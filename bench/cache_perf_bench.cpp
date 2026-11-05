/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// B3: instantiation cache baseline (Google Benchmark).
//
// Covers cold stores, verified hits, precise source/dependency misses,
// concurrent readers and full verification. Use `--benchmark_format=json` for
// a machine-readable baseline.
#include "SKL/AMC/Compilation/Cache.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

amc::InstantiationBuildInputs make_inputs(size_t index, const char *source = "source-v1") {
    amc::InstantiationRequest request;
    request.template_name = "abix_span";
    request.arguments = {"int32_t", "index_" + std::to_string(index)};
    request.arch = 1;
    request.os = 1;
    request.target_abi = 1;
    request.pointer_bits = 64;

    amc::InstantiationBuildInputs inputs;
    inputs.request = std::move(request);
    inputs.source_digest = amc::digest_text(source, amc::digest_domain_source);
    inputs.toolchain = amc::digest_text("clang-20", amc::digest_domain_toolchain);
    inputs.build_options = amc::digest_text("-O2", amc::digest_domain_options);
    return inputs;
}

bool write_text(const fs::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << text;
    return static_cast<bool>(output);
}

fs::path make_root(const char *name) {
    return fs::temp_directory_path()
         / (std::string(name) + "-" + std::to_string(Clock::now().time_since_epoch().count()));
}

class CacheStoreFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State &state) override {
        count = static_cast<size_t>(state.range(0));
        root = make_root("amc-cache-store");
        fs::remove_all(root);
        fs::create_directories(root);
        cache = std::make_unique<amc::InstantiationCache>(root.string());
    }

    void TearDown(const ::benchmark::State &) override {
        std::error_code error;
        fs::remove_all(root, error);
    }

    size_t count = 0;
    fs::path root;
    std::unique_ptr<amc::InstantiationCache> cache;
};

class CacheReadFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State &state) override {
        count = static_cast<size_t>(state.range(0));
        root = make_root("amc-cache-read");
        fs::remove_all(root);
        fs::create_directories(root);
        cache = std::make_unique<amc::InstantiationCache>(root.string());

        std::string error;
        for (size_t i = 0; i < count; ++i) {
            const std::vector<std::pair<std::string, std::string>> artifacts = {
                {    "module.abix", "artifact-" + std::to_string(i)},
                {"module.lowering", "lowering-" + std::to_string(i)},
            };
            if (!cache->store(make_inputs(i), artifacts, error)) throw std::runtime_error("cache store: " + error);
        }

        dependency_file = root / "dependency.txt";
        if (!write_text(dependency_file, "dependency-v1")) throw std::runtime_error("cannot write dependency file");
        dependency_inputs = make_inputs(count + 1);
        const std::vector<amc::CacheDependency> dependencies = {
            {dependency_file.string(), amc::digest_text("dependency-v1", amc::digest_domain_dependency)},
        };
        if (!cache->store(dependency_inputs,
                {
                    {"module.abix", "dependency-artifact"}
        },
                dependencies, error))
            throw std::runtime_error("dependency store: " + error);

        // Make the dependency benchmark observe a changed file on every lookup.
        if (!write_text(dependency_file, "dependency-v2")) throw std::runtime_error("cannot update dependency file");
    }

    void TearDown(const ::benchmark::State &) override {
        std::error_code error;
        fs::remove_all(root, error);
    }

    size_t count = 0;
    fs::path root;
    fs::path dependency_file;
    amc::InstantiationBuildInputs dependency_inputs;
    std::unique_ptr<amc::InstantiationCache> cache;
};

}   // namespace

BENCHMARK_DEFINE_F(CacheStoreFixture, StoreCold)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        state.PauseTiming();
        cache->clear();
        state.ResumeTiming();
        for (size_t i = 0; i < count; ++i) {
            const std::vector<std::pair<std::string, std::string>> artifacts = {
                {    "module.abix", "artifact-" + std::to_string(i)},
                {"module.lowering", "lowering-" + std::to_string(i)},
            };
            error.clear();
            bool ok = cache->store(make_inputs(i), artifacts, error);
            benchmark::DoNotOptimize(ok);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(CacheStoreFixture, StoreCold)->Arg(200)->Arg(1'000);

BENCHMARK_DEFINE_F(CacheReadFixture, LookupHit)(benchmark::State &state) {
    for (auto _ : state) {
        for (size_t i = 0; i < count; ++i) {
            auto result = cache->lookup(make_inputs(i));
            benchmark::DoNotOptimize(result);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(CacheReadFixture, LookupHit)->Arg(200)->Arg(1'000);

BENCHMARK_DEFINE_F(CacheReadFixture, LookupMiss)(benchmark::State &state) {
    for (auto _ : state) {
        for (size_t i = 0; i < count; ++i) {
            auto result = cache->lookup(make_inputs(i, "source-v2"));
            benchmark::DoNotOptimize(result);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(CacheReadFixture, LookupMiss)->Arg(200)->Arg(1'000);

BENCHMARK_DEFINE_F(CacheReadFixture, LookupMissDiagnose)(benchmark::State &state) {
    for (auto _ : state) {
        for (size_t i = 0; i < count; ++i) {
            auto result = cache->lookup(make_inputs(i, "source-v2"), amc::LookupOptions{true});
            benchmark::DoNotOptimize(result);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(CacheReadFixture, LookupMissDiagnose)->Arg(200);

BENCHMARK_DEFINE_F(CacheReadFixture, DependencyChanged)(benchmark::State &state) {
    for (auto _ : state) {
        auto result = cache->lookup(dependency_inputs);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(CacheReadFixture, DependencyChanged)->Arg(200)->Arg(1'000);

BENCHMARK_DEFINE_F(CacheReadFixture, LookupConcurrent)(benchmark::State &state) {
    // Google Benchmark fixtures share one instance across threads, so the
    // cache is initialized once here and worker threads are scoped to the
    // measured region. Thread creation is part of this micro-benchmark.
    constexpr size_t kThreads = 4;
    for (auto _ : state) {
        std::vector<std::thread> readers;
        readers.reserve(kThreads);
        for (size_t thread = 0; thread < kThreads; ++thread) {
            readers.emplace_back([&, thread] {
                for (size_t i = thread; i < count; i += kThreads) {
                    auto result = cache->lookup(make_inputs(i));
                    benchmark::DoNotOptimize(result);
                }
            });
        }
        for (auto &reader : readers)
            reader.join();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(CacheReadFixture, LookupConcurrent)->Arg(200)->Arg(1'000);

BENCHMARK_DEFINE_F(CacheReadFixture, VerifyAll)(benchmark::State &state) {
    std::vector<std::string> failures;
    for (auto _ : state) {
        failures.clear();
        size_t checked = cache->verify_all(failures);
        benchmark::DoNotOptimize(checked);
        benchmark::DoNotOptimize(failures);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(CacheReadFixture, VerifyAll)->Arg(200)->Arg(1'000);
