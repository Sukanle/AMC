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
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Verify.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/Compatibility.h"
#include "SKL/AMC/Pass/Artifact.h"
#include "SKL/AMC/Pass/Compatibility.h"
#include "SKL/AMC/Pass/Semantic.h"
#include "SKL/AMC/Pass/TypeMapping.h"
#include "SKL/AMC/Pass/Pipeline.h"
#include "SKL/AMC/Pass/Verify.h"

#include <fmt/base.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

skl::amc::Hash128 id(const char *text) { return skl::amc::hash_text(text, 0x54595045ULL); }

skl::amc::ABIModule module_with_layout(uint32_t size, uint32_t align) {
    skl::amc::ABIModule module;
    skl::amc::Type type;
    type.name = "demo::Widget";
    type.kind = skl::amc::TypeKind::RECORD;
    type.id = id("demo::Widget");
    type.size = size;
    type.align = align;
    module.types.push_back(type);
    return module;
}

void test_verify_abi_pass() {
    skl::amc::ir::Module module;
    module.abi = module_with_layout(8, 8);
    skl::amc::pass::Context context;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::verify_abi(module, context, diagnostics), "verify_abi accepts a valid module");
    check(skl::amc::diagnostics::error_count(diagnostics) == 0, "valid module produces no errors");

    skl::amc::Function broken;
    broken.name = "broken";
    broken.return_type = id("missing");
    module.abi.functions.push_back(broken);
    skl::amc::diagnostics::DiagnosticList invalid;
    check(!skl::amc::pass::verify_abi(module, context, invalid), "verify_abi rejects a broken module");
    check(skl::amc::diagnostics::has_errors(invalid), "broken module emits an error");
}

void test_compatibility_pass() {
    auto contract = module_with_layout(8, 8);
    auto implementation = module_with_layout(16, 8);
    contract.types[0].layout_hash = skl::amc::hash_text("layout-a", 0x4c41594fULL);
    implementation.types[0].layout_hash = skl::amc::hash_text("layout-b", 0x4c41594fULL);

    skl::amc::pass::Context context;
    context.strict = false;
    skl::amc::VerifyResult result;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::compare_modules(contract, implementation, result, context, diagnostics),
        "compatibility comparison completes");
    check(!result.consistent, "layout drift is reported");
    check(!diagnostics.empty(), "drift is emitted as a warning in non-strict mode");
    check(skl::amc::diagnostics::error_count(diagnostics) == 0, "non-strict drift is not an error");

    context.strict = true;
    skl::amc::diagnostics::DiagnosticList strict;
    check(skl::amc::pass::compare_modules(contract, implementation, result, context, strict),
        "strict comparison still completes");
    check(skl::amc::diagnostics::has_errors(strict), "strict drift becomes an error");
}

void test_diagnostics_aggregation() {
    skl::amc::diagnostics::DiagnosticList diagnostics;
    skl::amc::diagnostics::add_warning(diagnostics, "warn", "pass", "warning text");
    skl::amc::diagnostics::add_error(diagnostics, "err", "pass", "error text");
    const auto summary = skl::amc::diagnostics::summarize(diagnostics);
    check(summary.errors == 1 && summary.warnings == 1 && !summary.ok(), "diagnostic summary counts severities");
    const std::string json = skl::amc::diagnostics::diagnostics_to_json(diagnostics);
    check(json.find("\"schema\":\"amc.diagnostics/1\"") != std::string::npos, "diagnostics JSON schema");
    check(json.find("\"errors\":1") != std::string::npos && json.find("\"warnings\":1") != std::string::npos,
        "diagnostics JSON counts");
    check(skl::amc::diagnostics::diagnostics_to_text(diagnostics).find("pass") != std::string::npos,
        "diagnostics text includes the stage");

    skl::amc::diagnostics::Diagnostic located;
    located.severity = skl::amc::diagnostics::Severity::ERROR;
    located.code = "abi_layout_changed";
    located.stage = "verify";
    located.message = "ABI layout_changed";
    located.file = "widget.hpp";
    located.line = 12;
    located.column = 3;
    check(skl::amc::diagnostics::diagnostics_to_text({located}) == "widget.hpp:12:3: error: ABI layout_changed\n",
        "located diagnostics use compiler annotation format");
}

void test_verify_result_diagnostics() {
    auto contract = module_with_layout(8, 8);
    auto implementation = module_with_layout(16, 8);
    contract.types[0].layout_hash = skl::amc::hash_text("layout-a", 0x4c41594fULL);
    implementation.types[0].layout_hash = skl::amc::hash_text("layout-b", 0x4c41594fULL);

    skl::amc::VerifyResult result;
    std::string error;
    check(skl::amc::ir::compare_contracts(contract, implementation, result, error),
        "IR compatibility facade reports drift");
    const auto diagnostics = skl::amc::diagnostics::from_verify_result(contract, result);
    check(!diagnostics.empty(), "verify drift converts to diagnostics");
    check(diagnostics.front().stage == "verify", "verify diagnostics carry the verify stage");
    check(diagnostics.front().code == "abi_layout_changed", "verify diagnostics carry a stable code");
    const std::string json = skl::amc::diagnostics::diagnostics_to_json(diagnostics);
    check(json.find("\"stage\":\"verify\"") != std::string::npos, "verify diagnostics JSON keeps the stage");
    check(json.find("\"code\":\"abi_layout_changed\"") != std::string::npos, "verify diagnostics JSON keeps the code");
}

void test_verify_artifact_pass() {
    skl::amc::ir::Module module;
    module.schema_version = skl::amc::ir::module_schema_version;
    module.artifact.semantic_loaded = true;
    module.semantic.rules_version = 1;
    skl::amc::pass::Context context;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::verify_artifact(module, context, diagnostics), "verify_artifact accepts the current schema");

    module.schema_version = skl::amc::ir::module_schema_version + 1;
    skl::amc::diagnostics::DiagnosticList newer;
    check(!skl::amc::pass::verify_artifact(module, context, newer), "verify_artifact rejects a newer IR schema");
    check(skl::amc::diagnostics::has_errors(newer), "newer schema emits an error");

    module.schema_version = skl::amc::ir::module_schema_version;
    module.semantic.rules_version = 0;
    skl::amc::diagnostics::DiagnosticList bad_semantic;
    check(!skl::amc::pass::verify_artifact(module, context, bad_semantic),
        "verify_artifact rejects a semantic sidecar without rules version");
}

void test_verify_semantic_pass() {
    skl::amc::ir::Module module;
    skl::amc::semantic::SemanticType type;
    type.id = skl::amc::semantic::SemanticTypeId{id("demo::Widget")};
    type.kind = skl::amc::semantic::SemanticTypeKind::RECORD;
    type.qualified = "demo::Widget";
    module.semantic.types.push_back(type);

    skl::amc::pass::Context context;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::verify_semantic(module, context, diagnostics), "verify_semantic accepts complete records");

    skl::amc::semantic::SemanticRecord record;
    record.type = type.id;
    skl::amc::semantic::SemanticBase base;
    base.type = skl::amc::semantic::SemanticTypeId{id("demo::Missing")};
    record.bases.push_back(base);
    module.semantic.records.push_back(record);
    skl::amc::diagnostics::DiagnosticList invalid;
    check(!skl::amc::pass::verify_semantic(module, context, invalid), "verify_semantic rejects missing references");
    check(skl::amc::diagnostics::has_errors(invalid), "missing semantic reference emits an error");
}

void test_verify_lowering_pass() {
    skl::amc::ir::Module module;
    module.abi = module_with_layout(8, 8);
    module.abi.types[0].layout_hash = skl::amc::hash_text("layout", 0x4c41594fULL);

    skl::amc::ir::LoweredType lowered;
    lowered.name = "demo::Widget";
    lowered.abi = module.abi.types[0].id;
    lowered.abi_index = 0;
    lowered.status = skl::amc::ir::LoweringStatus::LOWERED;
    module.lowering.types.push_back(lowered);

    skl::amc::pass::Context context;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::verify_lowering(module, context, diagnostics), "verify_lowering accepts resolved references");

    module.lowering.types[0].abi = id("missing");
    skl::amc::diagnostics::DiagnosticList invalid;
    check(!skl::amc::pass::verify_lowering(module, context, invalid), "verify_lowering rejects missing ABI references");
    check(skl::amc::diagnostics::has_errors(invalid), "missing lowering reference emits an error");
}

void test_compatibility_mapping_pass() {
    auto source = module_with_layout(8, 8);
    auto target = module_with_layout(8, 8);
    source.types[0].layout_hash = skl::amc::hash_text("layout", 0x4c41594fULL);
    target.types[0].layout_hash = source.types[0].layout_hash;
    skl::amc::ABIModule report;
    skl::amc::pass::Context context;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::build_compatibility_report(source, target, report, context, diagnostics),
        "compatibility mapping report is produced");
}

void test_artifact_loader() {
    const std::string path = "amc-pass-test.abix";
    auto contract = module_with_layout(8, 8);
    contract.types[0].layout_hash = skl::amc::hash_text("layout", 0x4c41594fULL);
    std::string error;
    check(skl::amc::write_abix(contract, path, error), "artifact written");
    skl::amc::ir::Module loaded;
    check(skl::amc::ir::load_artifact(path, loaded, error), "artifact loaded into ir::Module");
    check(loaded.abi.types.size() == 1 && loaded.abi.types[0].name == "demo::Widget", "ABI view is loaded");
    std::remove(path.c_str());
}

void test_artifact_loader_options() {
    const std::string path = "amc-pass-options-test.abix";
    auto contract = module_with_layout(8, 8);
    contract.types[0].layout_hash = skl::amc::hash_text("layout", 0x4c41594fULL);
    std::string error;
    check(skl::amc::write_abix(contract, path, error), "artifact written for sidecar options");
    {
        std::ofstream semantic(path + ".semantic", std::ios::binary);
        semantic << "not semantic json";
        std::ofstream lowering(path + ".lowering", std::ios::binary);
        lowering << "not lowering json";
    }

    skl::amc::ir::LoadOptions abi_only;
    abi_only.semantic = false;
    abi_only.lowering = false;
    skl::amc::ir::Module loaded;
    check(skl::amc::ir::load_artifact(path, loaded, error, abi_only), "ABI-only load skips malformed sidecars");
    check(
        !loaded.artifact.semantic_loaded && !loaded.artifact.lowering_loaded, "ABI-only load marks sidecars unloaded");

    error.clear();
    skl::amc::ir::Module full;
    check(!skl::amc::ir::load_artifact(path, full, error), "full load rejects malformed sidecars");

    std::remove(path.c_str());
    std::remove((path + ".semantic").c_str());
    std::remove((path + ".lowering").c_str());
}

void test_default_pipeline() {
    skl::amc::ir::Module module;
    module.abi = module_with_layout(8, 8);
    module.abi.types[0].layout_hash = skl::amc::hash_text("layout", 0x4c41594fULL);
    skl::amc::diagnostics::DiagnosticList diagnostics;
    check(skl::amc::pass::run_default_pipeline(module, diagnostics), "default pipeline accepts a valid module");
    check(skl::amc::diagnostics::error_count(diagnostics) == 0, "default pipeline has no errors");
}

}   // namespace

int main() {
    test_verify_abi_pass();
    test_compatibility_pass();
    test_diagnostics_aggregation();
    test_verify_result_diagnostics();
    test_verify_artifact_pass();
    test_verify_semantic_pass();
    test_verify_lowering_pass();
    test_compatibility_mapping_pass();
    test_artifact_loader();
    test_artifact_loader_options();
    test_default_pipeline();

    if (failures != 0) {
        fmt::print(stderr, "amc-pass-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-pass-test: all checks passed\n");
    return 0;
}
