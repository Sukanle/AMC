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
// B2: Artifact / Metadata / Materialize baseline (Google Benchmark).
//
// The fixture uses the public artifact APIs and a generated module, so it
// exercises the same read/write/metadata/materialization path as `amc build`,
// `amc verify` and the MCP server. Use `--benchmark_format=json` for a
// machine-readable baseline.
#include "SKL/AMC/Artifact/Materialize.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/Lowering.h"
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Semantic/Model.h"
#include "SKL/AMC/Semantic/Serialize.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

amc::AbiModule make_module(size_t count) {
    amc::AbiModule module;
    module.package_name = "artifact_perf";
    module.package_version = "1.0";

    amc::Type int_type;
    int_type.name = "int";
    int_type.kind = amc::TypeKind::primitive;
    int_type.id = amc::hash_text("int", 0x54595045ULL);
    int_type.size = 4;
    int_type.align = 4;
    int_type.primitive_abi = amc::primitive_abi(4, amc::PrimitiveAbiKind::sint, amc::FloatFormat::none, true);
    module.types.push_back(int_type);

    module.types.reserve(count + 1);
    module.fields.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = "perf::R" + std::to_string(i);
        amc::Type type;
        type.name = name;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(name, 0x54595045ULL);
        type.size = 4;
        type.align = 4;
        type.field_begin = static_cast<uint32_t>(module.fields.size());
        type.field_count = 1;

        amc::Field field;
        field.owner_type = type.id;
        field.name = "value";
        field.type_id = int_type.id;
        field.offset = 0;
        module.fields.push_back(field);
        module.types.push_back(std::move(type));
    }

    module.functions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = "perf::fn" + std::to_string(i);
        amc::Function function;
        function.name = name;
        function.signature = amc::hash_text(name + ":signature", 0x5349474eULL);
        function.return_type = int_type.id;
        amc::Parameter parameter;
        parameter.name = "value";
        parameter.type_id = int_type.id;
        function.parameters.push_back(std::move(parameter));
        module.functions.push_back(std::move(function));
    }

    amc::recompute_layout_hashes(module);
    return module;
}

amc::semantic::SemanticModule make_semantic(const amc::AbiModule &module) {
    amc::semantic::SemanticModule semantic;
    semantic.package_name = module.package_name;
    semantic.package_version = module.package_version;
    semantic.rules_version = 1;
    semantic.types.reserve(module.types.size());
    semantic.functions.reserve(module.functions.size());

    for (const auto &type : module.types) {
        if (type.kind != amc::TypeKind::record) continue;
        amc::semantic::SemanticType item;
        item.id = amc::semantic::SemanticTypeId{type.id};
        item.kind = amc::semantic::SemanticTypeKind::record;
        item.qualified_name = type.name;
        item.abi_type = type.id;
        semantic.types.push_back(std::move(item));
    }

    for (const auto &function : module.functions) {
        amc::semantic::SemanticFunction item;
        item.id = amc::semantic::SemanticFunctionId{amc::hash_text(function.name + ":semantic", 0x53454e46ULL)};
        item.kind = amc::semantic::SemanticFunctionKind::free_function;
        item.qualified_name = function.name;
        item.unqualified_name = function.name;
        item.abi_signature = function.signature;
        semantic.functions.push_back(std::move(item));
    }
    return semantic;
}

amc::ir::LoweringResult make_lowering(const amc::AbiModule &module) {
    amc::ir::LoweringResult lowering;
    lowering.rules_version = 1;
    lowering.types.reserve(module.types.size());
    lowering.functions.reserve(module.functions.size());

    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        if (type.kind != amc::TypeKind::record) continue;
        amc::ir::LoweredType lowered;
        lowered.semantic = amc::semantic::SemanticTypeId{type.id};
        lowered.abi = type.id;
        lowered.abi_index = static_cast<uint32_t>(i);
        lowered.name = type.name;
        lowered.status = amc::ir::LoweringStatus::lowered;
        lowered.reason = amc::ir::LoweringReason::ok;
        lowered.size = type.size;
        lowered.align = type.align;
        lowered.layout_hash = type.layout_hash;
        lowering.types.push_back(std::move(lowered));
    }

    for (const auto &function : module.functions) {
        amc::ir::LoweredFunction lowered;
        lowered.semantic =
            amc::semantic::SemanticFunctionId{amc::hash_text(function.name + ":semantic", 0x53454e46ULL)};
        lowered.abi = function.signature;
        lowered.name = function.name;
        lowered.status = amc::ir::LoweringStatus::lowered;
        lowered.reason = amc::ir::LoweringReason::ok;
        lowered.signature = function.signature;
        lowered.return_type = function.return_type;
        lowering.functions.push_back(std::move(lowered));
    }
    return lowering;
}

class ArtifactFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State &state) override {
        count = static_cast<size_t>(state.range(0));
        module = make_module(count);

        directory = fs::temp_directory_path()
                  / ("amc-artifact-perf-"
                      + std::to_string(count)
                      + "-"
                      + std::to_string(Clock::now().time_since_epoch().count()));
        fs::remove_all(directory);
        fs::create_directories(directory);
        artifact = directory / "perf.abix";
        semantic_path = fs::path(artifact.string() + ".semantic");
        lowering_path = fs::path(artifact.string() + ".lowering");

        std::string error;
        if (!amc::write_abix(module, artifact.string(), error)) throw std::runtime_error("write_abix: " + error);
        if (!amc::semantic::write_semantic_json(make_semantic(module), semantic_path.string(), error))
            throw std::runtime_error("write_semantic: " + error);
        if (!amc::ir::write_lowering_json(make_lowering(module), lowering_path.string(), error))
            throw std::runtime_error("write_lowering: " + error);

        amc::MetadataOptions options;
        options.build_id = amc::hash_text("artifact-perf-build", 0x4255494cULL);
        if (!amc::build_metadata_region(module, options, region, error))
            throw std::runtime_error("build_metadata_region: " + error);
    }

    void TearDown(const ::benchmark::State &) override {
        std::error_code error;
        fs::remove_all(directory, error);
    }

    size_t count = 0;
    amc::AbiModule module;
    fs::path directory;
    fs::path artifact;
    fs::path semantic_path;
    fs::path lowering_path;
    std::vector<uint8_t> region;
};

}   // namespace

BENCHMARK_DEFINE_F(ArtifactFixture, WriteAbix)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        error.clear();
        bool ok = amc::write_abix(module, artifact.string(), error);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, WriteAbix)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, ReadAbix)(benchmark::State &state) {
    amc::AbiModule decoded;
    std::string error;
    for (auto _ : state) {
        decoded = {};
        error.clear();
        bool ok = amc::read_abix(artifact.string(), decoded, error);
        benchmark::DoNotOptimize(decoded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, ReadAbix)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, LoadArtifact)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        amc::ir::Module loaded;
        error.clear();
        bool ok = amc::ir::load_artifact(artifact.string(), loaded, error);
        benchmark::DoNotOptimize(loaded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, LoadArtifact)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, BuildMetadata)(benchmark::State &state) {
    amc::MetadataOptions options;
    options.build_id = amc::hash_text("artifact-perf-build", 0x4255494cULL);
    std::string error;
    for (auto _ : state) {
        std::vector<uint8_t> output;
        error.clear();
        bool ok = amc::build_metadata_region(module, options, output, error);
        benchmark::DoNotOptimize(output);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, BuildMetadata)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, VerifyMetadata)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        error.clear();
        bool ok = amc::verify_metadata_region(region, error);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, VerifyMetadata)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, ModuleFromRegion)(benchmark::State &state) {
    amc::AbiModule decoded;
    std::string error;
    for (auto _ : state) {
        decoded = {};
        error.clear();
        bool ok = amc::module_from_region(region, decoded, error);
        benchmark::DoNotOptimize(decoded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, ModuleFromRegion)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, Materialize)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        amc::MaterializedModule materialized;
        error.clear();
        bool ok = materialized.build(module, error);
        benchmark::DoNotOptimize(materialized);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, Materialize)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, MaterializeRegion)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        amc::MaterializedModule materialized;
        error.clear();
        bool ok = materialized.build_from_region(region, error);
        benchmark::DoNotOptimize(materialized);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, MaterializeRegion)->Arg(5'000)->Arg(10'000);
