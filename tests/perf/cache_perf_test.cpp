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
// B3: instantiation cache baseline.
//
// Measures the cache path that matters for incremental builds: cold stores,
// verified hits, precise misses, dependency re-hashing, concurrent lookups and
// full verification. It prints one JSON record; timing is not a CI assertion.
#include "SKL/AMC/Compilation/Cache.h"

#include <fmt/base.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#  include <sys/resource.h>
#endif

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

struct Measurement {
    const char *name;
    double milliseconds;
};

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

long long peak_rss_kb() {
#if defined(__linux__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) return static_cast<long long>(usage.ru_maxrss);
#endif
    return 0;
}

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

void print_metric(std::vector<Measurement> &metrics, const char *name, double milliseconds) {
    metrics.push_back({name, milliseconds});
}

}   // namespace

int main() {
    const bool large = std::getenv("AMC_CACHE_PERF_LARGE") != nullptr;
    const size_t count = large ? 1'000 : 200;

    const fs::path root = fs::temp_directory_path()
                        / ("amc-cache-perf-"
                            + std::to_string(count)
                            + "-"
                            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error_code;
    fs::remove_all(root, error_code);
    fs::create_directories(root);

    amc::InstantiationCache cache(root.string());
    std::string error;
    std::vector<Measurement> metrics;

    auto start = Clock::now();
    for (size_t i = 0; i < count; ++i) {
        const auto inputs = make_inputs(i);
        const std::vector<std::pair<std::string, std::string>> artifacts = {
            {    "module.abix", "artifact-" + std::to_string(i)},
            {"module.lowering", "lowering-" + std::to_string(i)},
        };
        if (!cache.store(inputs, artifacts, error)) {
            fmt::print(stderr, "amc-cache-perf-test: store failed: {}\n", error);
            return 1;
        }
    }
    print_metric(metrics, "store_cold", elapsed_ms(start));

    size_t hits = 0;
    start = Clock::now();
    for (size_t i = 0; i < count; ++i) {
        const auto result = cache.lookup(make_inputs(i));
        if (result.status == amc::CacheStatus::hit) ++hits;
    }
    print_metric(metrics, "lookup_hit", elapsed_ms(start));
    if (hits != count) {
        fmt::print(stderr, "amc-cache-perf-test: expected {} hits, got {}\n", count, hits);
        return 1;
    }

    size_t misses = 0;
    start = Clock::now();
    for (size_t i = 0; i < count; ++i) {
        const auto result = cache.lookup(make_inputs(i, "source-v2"));
        if (result.status == amc::CacheStatus::miss && result.reason == amc::cache_reason_source_digest_changed)
            ++misses;
    }
    print_metric(metrics, "lookup_miss", elapsed_ms(start));
    if (misses != count) {
        fmt::print(stderr, "amc-cache-perf-test: expected {} source misses, got {}\n", count, misses);
        return 1;
    }

    // Dependency re-hashing: one entry depends on a file whose digest is part
    // of the reuse decision.
    const fs::path dependency_file = root / "dependency.txt";
    if (!write_text(dependency_file, "dependency-v1")) {
        fmt::print(stderr, "amc-cache-perf-test: cannot write dependency file\n");
        return 1;
    }
    const auto dependency_inputs = make_inputs(count + 1);
    const std::vector<amc::CacheDependency> dependencies = {
        {dependency_file.string(), amc::digest_text("dependency-v1", amc::digest_domain_dependency)},
    };
    if (!cache.store(dependency_inputs,
            {
                {"module.abix", "dependency-artifact"}
    },
            dependencies, error)) {
        fmt::print(stderr, "amc-cache-perf-test: dependency store failed: {}\n", error);
        return 1;
    }

    start = Clock::now();
    const auto dependency_hit = cache.lookup(dependency_inputs);
    print_metric(metrics, "dependency_hit", elapsed_ms(start));
    if (dependency_hit.status != amc::CacheStatus::hit) {
        fmt::print(stderr, "amc-cache-perf-test: dependency hit failed: {}\n", dependency_hit.reason);
        return 1;
    }

    if (!write_text(dependency_file, "dependency-v2")) {
        fmt::print(stderr, "amc-cache-perf-test: cannot update dependency file\n");
        return 1;
    }
    start = Clock::now();
    const auto dependency_miss = cache.lookup(dependency_inputs);
    print_metric(metrics, "dependency_changed", elapsed_ms(start));
    if (dependency_miss.status != amc::CacheStatus::miss
        || dependency_miss.reason != amc::cache_reason_dependency_changed) {
        fmt::print(stderr, "amc-cache-perf-test: dependency miss was not reported\n");
        return 1;
    }

    // Concurrent readers exercise the shared-cache lookup path.
    std::atomic<size_t> concurrent_hits{0};
    constexpr size_t kThreads = 4;
    start = Clock::now();
    std::vector<std::thread> readers;
    readers.reserve(kThreads);
    for (size_t thread = 0; thread < kThreads; ++thread) {
        readers.emplace_back([&, thread] {
            for (size_t i = thread; i < count; i += kThreads) {
                if (cache.lookup(make_inputs(i)).status == amc::CacheStatus::hit) ++concurrent_hits;
            }
        });
    }
    for (auto &reader : readers)
        reader.join();
    print_metric(metrics, "lookup_concurrent", elapsed_ms(start));
    if (concurrent_hits.load() != count) {
        fmt::print(stderr, "amc-cache-perf-test: concurrent hits expected {}, got {}\n", count, concurrent_hits.load());
        return 1;
    }

    start = Clock::now();
    std::vector<std::string> failures;
    const size_t checked = cache.verify_all(failures);
    print_metric(metrics, "verify_all", elapsed_ms(start));
    if (checked < count || !failures.empty()) {
        fmt::print(
            stderr, "amc-cache-perf-test: verify_all checked {} entries, {} failures\n", checked, failures.size());
        return 1;
    }

    fmt::print(
        "{{\"schema\":\"amc.bench/1\",\"benchmark\":\"cache\",\"count\":{},\"entries\":{},\"peak_rss_kb\":{},"
        "\"metrics\":[",
        count, cache.entries(), peak_rss_kb());
    for (size_t i = 0; i < metrics.size(); ++i) {
        if (i != 0) fmt::print(",");
        fmt::print("{{\"name\":\"{}\",\"ms\":{:.3f}}}", metrics[i].name, metrics[i].milliseconds);
    }
    fmt::print("]}}\n");

    fs::remove_all(root, error_code);
    return 0;
}
