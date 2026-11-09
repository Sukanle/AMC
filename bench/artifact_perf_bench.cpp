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

skl::amc::ABIModule make_module(size_t count) {
    skl::amc::ABIModule module;
    module.package_name = "artifact_perf";
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
        const std::string name = "perf::R" + std::to_string(i);
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
        const std::string name = "perf::fn" + std::to_string(i);
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

skl::amc::semantic::SemanticModule make_semantic(const skl::amc::ABIModule &module) {
    skl::amc::semantic::SemanticModule semantic;
    semantic.package_name = module.package_name;
    semantic.package_version = module.package_version;
    semantic.rules_version = 1;
    semantic.types.reserve(module.types.size());
    semantic.functions.reserve(module.functions.size());

    for (const auto &type : module.types) {
        if (type.kind != skl::amc::TypeKind::RECORD) continue;
        skl::amc::semantic::SemanticType item;
        item.id = skl::amc::semantic::SemanticTypeId{type.id};
        item.kind = skl::amc::semantic::SemanticTypeKind::RECORD;
        item.qualified = type.name;
        item.abi_type = type.id;
        semantic.types.push_back(std::move(item));
    }

    for (const auto &function : module.functions) {
        skl::amc::semantic::SemanticFunc item;
        item.id = skl::amc::semantic::SemanticFuncId{skl::amc::hash_text(function.name + ":semantic", 0x53454e46ULL)};
        item.kind = skl::amc::semantic::SemanticFuncKind::FREE;
        item.qualified = function.name;
        item.unqualified = function.name;
        item.abi_signature = function.signature;
        semantic.functions.push_back(std::move(item));
    }
    return semantic;
}

skl::amc::ir::LoweringResult make_lowering(const skl::amc::ABIModule &module) {
    skl::amc::ir::LoweringResult lowering;
    lowering.rules_version = 1;
    lowering.types.reserve(module.types.size());
    lowering.functions.reserve(module.functions.size());

    for (size_t i = 0; i < module.types.size(); ++i) {
        const auto &type = module.types[i];
        if (type.kind != skl::amc::TypeKind::RECORD) continue;
        skl::amc::ir::LoweredType lowered;
        lowered.semantic = skl::amc::semantic::SemanticTypeId{type.id};
        lowered.abi = type.id;
        lowered.abi_index = static_cast<uint32_t>(i);
        lowered.name = type.name;
        lowered.status = skl::amc::ir::LoweringStatus::LOWERED;
        lowered.reason = skl::amc::ir::LoweringReason::OK;
        lowered.size = type.size;
        lowered.align = type.align;
        lowered.layout_hash = type.layout_hash;
        lowering.types.push_back(std::move(lowered));
    }

    for (const auto &function : module.functions) {
        skl::amc::ir::LoweredFunction lowered;
        lowered.semantic =
            skl::amc::semantic::SemanticFuncId{skl::amc::hash_text(function.name + ":semantic", 0x53454e46ULL)};
        lowered.abi = function.signature;
        lowered.name = function.name;
        lowered.status = skl::amc::ir::LoweringStatus::LOWERED;
        lowered.reason = skl::amc::ir::LoweringReason::OK;
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
        if (!skl::amc::write_abix(module, artifact.string(), error)) throw std::runtime_error("write_abix: " + error);
        if (!skl::amc::semantic::write_semantic_json(make_semantic(module), semantic_path.string(), error))
            throw std::runtime_error("write_semantic: " + error);
        if (!skl::amc::ir::write_lowering_json(make_lowering(module), lowering_path.string(), error))
            throw std::runtime_error("write_lowering: " + error);

        skl::amc::MetadataOptions options;
        options.build_id = skl::amc::hash_text("artifact-perf-build", 0x4255494cULL);
        if (!skl::amc::build_metadata_region(module, options, region, error))
            throw std::runtime_error("build_metadata_region: " + error);
    }

    void TearDown(const ::benchmark::State &) override {
        std::error_code error;
        fs::remove_all(directory, error);
    }

    size_t count = 0;
    skl::amc::ABIModule module;
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
        bool ok = skl::amc::write_abix(module, artifact.string(), error);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, WriteAbix)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, ReadAbix)(benchmark::State &state) {
    skl::amc::ABIModule decoded;
    std::string error;
    for (auto _ : state) {
        decoded = {};
        error.clear();
        bool ok = skl::amc::read_abix(artifact.string(), decoded, error);
        benchmark::DoNotOptimize(decoded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, ReadAbix)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, LoadArtifact)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        skl::amc::ir::Module loaded;
        error.clear();
        bool ok = skl::amc::ir::load_artifact(artifact.string(), loaded, error);
        benchmark::DoNotOptimize(loaded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, LoadArtifact)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, LoadAbiOnly)(benchmark::State &state) {
    skl::amc::ir::LoadOptions options;
    options.semantic = false;
    options.lowering = false;
    std::string error;
    for (auto _ : state) {
        skl::amc::ir::Module loaded;
        error.clear();
        bool ok = skl::amc::ir::load_artifact(artifact.string(), loaded, error, options);
        benchmark::DoNotOptimize(loaded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, LoadAbiOnly)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, LoadWithSemantic)(benchmark::State &state) {
    skl::amc::ir::LoadOptions options;
    options.lowering = false;
    std::string error;
    for (auto _ : state) {
        skl::amc::ir::Module loaded;
        error.clear();
        bool ok = skl::amc::ir::load_artifact(artifact.string(), loaded, error, options);
        benchmark::DoNotOptimize(loaded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, LoadWithSemantic)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, LoadWithLowering)(benchmark::State &state) {
    skl::amc::ir::LoadOptions options;
    options.semantic = false;
    std::string error;
    for (auto _ : state) {
        skl::amc::ir::Module loaded;
        error.clear();
        bool ok = skl::amc::ir::load_artifact(artifact.string(), loaded, error, options);
        benchmark::DoNotOptimize(loaded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, LoadWithLowering)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, BuildMetadata)(benchmark::State &state) {
    skl::amc::MetadataOptions options;
    options.build_id = skl::amc::hash_text("artifact-perf-build", 0x4255494cULL);
    std::string error;
    for (auto _ : state) {
        std::vector<uint8_t> output;
        error.clear();
        bool ok = skl::amc::build_metadata_region(module, options, output, error);
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
        bool ok = skl::amc::verify_metadata_region(region, error);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, VerifyMetadata)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, ModuleFromRegion)(benchmark::State &state) {
    skl::amc::ABIModule decoded;
    std::string error;
    for (auto _ : state) {
        decoded = {};
        error.clear();
        bool ok = skl::amc::module_from_region(region, decoded, error);
        benchmark::DoNotOptimize(decoded);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, ModuleFromRegion)->Arg(5'000)->Arg(10'000);

BENCHMARK_DEFINE_F(ArtifactFixture, Materialize)(benchmark::State &state) {
    std::string error;
    for (auto _ : state) {
        skl::amc::MaterializedModule materialized;
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
        skl::amc::MaterializedModule materialized;
        error.clear();
        bool ok = materialized.build_from_region(region, error);
        benchmark::DoNotOptimize(materialized);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(ArtifactFixture, MaterializeRegion)->Arg(5'000)->Arg(10'000);
