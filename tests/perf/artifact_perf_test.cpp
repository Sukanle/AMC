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
// B2: Artifact / Metadata / Materialize baseline.
//
// The benchmark deliberately uses the public artifact APIs and a generated
// module, so it exercises the same read/write/metadata/materialization path as
// `amc build`, `amc verify` and the MCP server. It prints one JSON record and
// only fails on an actual API error; timing is a baseline, not a CI assertion.
#include "SKL/AMC/Artifact/Materialize.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/Lowering.h"
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Semantic/Model.h"
#include "SKL/AMC/Semantic/Serialize.h"

#include <fmt/base.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
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

std::uintmax_t file_bytes(const fs::path &path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    return error ? 0 : static_cast<std::uintmax_t>(size);
}

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

void print_metric(std::vector<Measurement> &metrics, const char *name, double milliseconds) {
    metrics.push_back({name, milliseconds});
}

}   // namespace

int main() {
    const bool large = std::getenv("AMC_ARTIFACT_PERF_LARGE") != nullptr;
    const size_t count = large ? 10'000 : 5'000;

    const amc::AbiModule module = make_module(count);
    std::string error;

    const fs::path directory = fs::temp_directory_path()
                             / ("amc-artifact-perf-"
                                 + std::to_string(count)
                                 + "-"
                                 + std::to_string(Clock::now().time_since_epoch().count()));
    std::error_code remove_error;
    fs::remove_all(directory, remove_error);
    fs::create_directories(directory);
    const fs::path artifact = directory / "perf.abix";

    std::vector<Measurement> metrics;

    auto start = Clock::now();
    const bool wrote = amc::write_abix(module, artifact.string(), error);
    print_metric(metrics, "write_abix", elapsed_ms(start));
    if (!wrote) {
        fmt::print(stderr, "amc-artifact-perf-test: write_abix failed: {}\n", error);
        return 1;
    }

    amc::AbiModule decoded;
    start = Clock::now();
    const bool read = amc::read_abix(artifact.string(), decoded, error);
    print_metric(metrics, "read_abix", elapsed_ms(start));
    if (!read) {
        fmt::print(stderr, "amc-artifact-perf-test: read_abix failed: {}\n", error);
        return 1;
    }

    const auto semantic = make_semantic(module);
    const auto lowering = make_lowering(module);
    const fs::path semantic_path = fs::path(artifact.string() + ".semantic");
    const fs::path lowering_path = fs::path(artifact.string() + ".lowering");

    start = Clock::now();
    const bool wrote_semantic = amc::semantic::write_semantic_json(semantic, semantic_path.string(), error);
    print_metric(metrics, "write_semantic", elapsed_ms(start));
    if (!wrote_semantic) {
        fmt::print(stderr, "amc-artifact-perf-test: write_semantic failed: {}\n", error);
        return 1;
    }

    start = Clock::now();
    const bool wrote_lowering = amc::ir::write_lowering_json(lowering, lowering_path.string(), error);
    print_metric(metrics, "write_lowering", elapsed_ms(start));
    if (!wrote_lowering) {
        fmt::print(stderr, "amc-artifact-perf-test: write_lowering failed: {}\n", error);
        return 1;
    }

    amc::ir::Module loaded;
    start = Clock::now();
    const bool loaded_ok = amc::ir::load_artifact(artifact.string(), loaded, error);
    print_metric(metrics, "load_artifact", elapsed_ms(start));
    if (!loaded_ok) {
        fmt::print(stderr, "amc-artifact-perf-test: load_artifact failed: {}\n", error);
        return 1;
    }

    amc::MetadataOptions options;
    options.build_id = amc::hash_text("artifact-perf-build", 0x4255494cULL);
    std::vector<uint8_t> region;
    start = Clock::now();
    const bool built_region = amc::build_metadata_region(module, options, region, error);
    print_metric(metrics, "build_metadata", elapsed_ms(start));
    if (!built_region) {
        fmt::print(stderr, "amc-artifact-perf-test: build_metadata_region failed: {}\n", error);
        return 1;
    }

    start = Clock::now();
    const bool verified_region = amc::verify_metadata_region(region, error);
    print_metric(metrics, "verify_metadata", elapsed_ms(start));
    if (!verified_region) {
        fmt::print(stderr, "amc-artifact-perf-test: verify_metadata_region failed: {}\n", error);
        return 1;
    }

    amc::AbiModule region_module;
    start = Clock::now();
    const bool decoded_region = amc::module_from_region(region, region_module, error);
    print_metric(metrics, "module_from_region", elapsed_ms(start));
    if (!decoded_region) {
        fmt::print(stderr, "amc-artifact-perf-test: module_from_region failed: {}\n", error);
        return 1;
    }

    amc::MaterializedModule materialized;
    start = Clock::now();
    const bool materialized_ok = materialized.build(module, error);
    print_metric(metrics, "materialize", elapsed_ms(start));
    if (!materialized_ok) {
        fmt::print(stderr, "amc-artifact-perf-test: materialize failed: {}\n", error);
        return 1;
    }

    amc::MaterializedModule region_materialized;
    start = Clock::now();
    const bool region_materialized_ok = region_materialized.build_from_region(region, error);
    print_metric(metrics, "materialize_region", elapsed_ms(start));
    if (!region_materialized_ok) {
        fmt::print(stderr, "amc-artifact-perf-test: materialize_region failed: {}\n", error);
        return 1;
    }

    fmt::print(
        "{{\"schema\":\"amc.bench/1\",\"benchmark\":\"artifact\",\"count\":{},\"artifact_bytes\":{},"
        "\"semantic_bytes\":{},\"lowering_bytes\":{},\"metadata_bytes\":{},\"peak_rss_kb\":{},\"metrics\":[",
        count, file_bytes(artifact), file_bytes(semantic_path), file_bytes(lowering_path), region.size(),
        peak_rss_kb());
    for (size_t i = 0; i < metrics.size(); ++i) {
        if (i != 0) fmt::print(",");
        fmt::print("{{\"name\":\"{}\",\"ms\":{:.3f}}}", metrics[i].name, metrics[i].milliseconds);
    }
    fmt::print("]}}\n");

    fs::remove_all(directory, remove_error);
    return 0;
}
