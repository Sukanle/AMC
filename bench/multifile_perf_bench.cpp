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
// OPT-6: multi-file artifact throughput baseline.
//
// The fixture writes `files` independent `.abix` artifacts and compares a
// sequential load with an llvm::ThreadPool load. The parallel path only shares
// immutable input paths, so each worker owns its output Module and error
// string. Use this benchmark before adding a production process/thread pool.
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/Module.h"

#include <benchmark/benchmark.h>

#include <llvm/Support/ThreadPool.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

skl::amc::ABIModule make_module(size_t count) {
    skl::amc::ABIModule module;
    module.package_name = "multifile_perf";
    module.package_version = "1.0";

    skl::amc::Type int_type;
    int_type.name = "int";
    int_type.kind = skl::amc::TypeKind::PRIMITIVE;
    int_type.id = skl::amc::hash_text("int", 0x54595045ULL);
    int_type.size = 4;
    int_type.align = 4;
    int_type.primitive_abi =
        skl::amc::primitive_abi(4, skl::amc::PrimitiveABIKind::AMC_SINT, skl::amc::FloatFormat::NONE, true);
    module.types.push_back(int_type);

    module.types.reserve(count + 1);
    module.fields.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = "multifile::R" + std::to_string(i);
        skl::amc::Type type;
        type.name = name;
        type.kind = skl::amc::TypeKind::RECORD;
        type.id = skl::amc::hash_text(name, 0x54595045ULL);
        type.size = 4;
        type.align = 4;
        type.field_begin = static_cast<uint32_t>(module.fields.size());
        type.field_count = 1;

        skl::amc::Field field;
        field.owner_type = type.id;
        field.name = "value";
        field.type_id = int_type.id;
        field.offset = 0;
        module.fields.push_back(field);
        module.types.push_back(std::move(type));
    }

    module.functions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = "multifile::fn" + std::to_string(i);
        skl::amc::Function function;
        function.name = name;
        function.signature = skl::amc::hash_text(name + ":signature", 0x5349474eULL);
        function.return_type = int_type.id;
        skl::amc::Parameter parameter;
        parameter.name = "value";
        parameter.type_id = int_type.id;
        function.params.push_back(std::move(parameter));
        module.functions.push_back(std::move(function));
    }

    skl::amc::recompute_layout_hashes(module);
    return module;
}

class MultifileFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State &state) override {
        files = static_cast<size_t>(state.range(0));
        directory = fs::temp_directory_path()
                  / ("amc-multifile-perf-"
                      + std::to_string(files)
                      + "-"
                      + std::to_string(Clock::now().time_since_epoch().count()));
        fs::remove_all(directory);
        fs::create_directories(directory);

        const skl::amc::ABIModule module = make_module(1'000);
        paths.clear();
        paths.reserve(files);
        std::string error;
        for (size_t i = 0; i < files; ++i) {
            const fs::path path = directory / ("file" + std::to_string(i) + ".abix");
            if (!skl::amc::write_abix(module, path.string(), error)) throw std::runtime_error("write_abix: " + error);
            paths.push_back(path.string());
        }
        pool = std::make_unique<llvm::DefaultThreadPool>();
    }

    void TearDown(const ::benchmark::State &) override {
        pool.reset();
        std::error_code error;
        fs::remove_all(directory, error);
    }

    size_t files = 0;
    fs::path directory;
    std::vector<std::string> paths;
    std::unique_ptr<llvm::DefaultThreadPool> pool;
};

BENCHMARK_DEFINE_F(MultifileFixture, SequentialAbiLoad)(benchmark::State &state) {
    skl::amc::ir::LoadOptions options;
    options.semantic = false;
    options.lowering = false;
    for (auto _ : state) {
        size_t types = 0;
        for (const auto &path : paths) {
            skl::amc::ir::Module loaded;
            std::string error;
            if (!skl::amc::ir::load_artifact(path, loaded, error, options))
                throw std::runtime_error("load_artifact: " + error);
            types += loaded.abi.types.size();
        }
        benchmark::DoNotOptimize(types);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(files));
}
BENCHMARK_REGISTER_F(MultifileFixture, SequentialAbiLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->UseRealTime();

BENCHMARK_DEFINE_F(MultifileFixture, ParallelAbiLoad)(benchmark::State &state) {
    skl::amc::ir::LoadOptions options;
    options.semantic = false;
    options.lowering = false;
    std::vector<skl::amc::ir::Module> loaded(paths.size());
    std::vector<std::string> errors(paths.size());
    for (auto _ : state) {
        for (auto &module : loaded)
            module = {};
        for (auto &error : errors)
            error.clear();
        for (size_t i = 0; i < paths.size(); ++i) {
            pool->async([&, i] {
                if (!skl::amc::ir::load_artifact(paths[i], loaded[i], errors[i], options))
                    errors[i] = "load_artifact: " + errors[i];
            });
        }
        pool->wait();

        size_t types = 0;
        for (size_t i = 0; i < loaded.size(); ++i) {
            if (!errors[i].empty()) throw std::runtime_error(errors[i]);
            types += loaded[i].abi.types.size();
        }
        benchmark::DoNotOptimize(types);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(files));
}
BENCHMARK_REGISTER_F(MultifileFixture, ParallelAbiLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->UseRealTime();

}   // namespace
