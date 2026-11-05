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
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/Pass/Compatibility.h"
#include "SKL/AMC/Pass/Pipeline.h"
#include "SKL/AMC/Pass/Verify.h"

#include <fmt/base.h>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

amc::Hash128 id(const char *text) { return amc::hash_text(text, 0x54595045ULL); }

amc::AbiModule module_with_layout(uint32_t size, uint32_t align) {
    amc::AbiModule module;
    amc::Type type;
    type.name = "demo::Widget";
    type.kind = amc::TypeKind::record;
    type.id = id("demo::Widget");
    type.size = size;
    type.align = align;
    module.types.push_back(type);
    return module;
}

void test_verify_abi_pass() {
    amc::ir::Module module;
    module.abi = module_with_layout(8, 8);
    amc::pass::Context context;
    amc::diagnostics::DiagnosticList diagnostics;
    check(amc::pass::verify_abi(module, context, diagnostics), "verify_abi accepts a valid module");
    check(amc::diagnostics::error_count(diagnostics) == 0, "valid module produces no errors");

    amc::Function broken;
    broken.name = "broken";
    broken.return_type = id("missing");
    module.abi.functions.push_back(broken);
    amc::diagnostics::DiagnosticList invalid;
    check(!amc::pass::verify_abi(module, context, invalid), "verify_abi rejects a broken module");
    check(amc::diagnostics::has_errors(invalid), "broken module emits an error");
}

void test_compatibility_pass() {
    auto contract = module_with_layout(8, 8);
    auto implementation = module_with_layout(16, 8);
    contract.types[0].layout_hash = amc::hash_text("layout-a", 0x4c41594fULL);
    implementation.types[0].layout_hash = amc::hash_text("layout-b", 0x4c41594fULL);

    amc::pass::Context context;
    context.strict = false;
    amc::VerifyResult result;
    amc::diagnostics::DiagnosticList diagnostics;
    check(amc::pass::compare_modules(contract, implementation, result, context, diagnostics),
        "compatibility comparison completes");
    check(!result.consistent, "layout drift is reported");
    check(!diagnostics.empty(), "drift is emitted as a warning in non-strict mode");
    check(amc::diagnostics::error_count(diagnostics) == 0, "non-strict drift is not an error");

    context.strict = true;
    amc::diagnostics::DiagnosticList strict;
    check(amc::pass::compare_modules(contract, implementation, result, context, strict),
        "strict comparison still completes");
    check(amc::diagnostics::has_errors(strict), "strict drift becomes an error");
}

void test_artifact_loader() {
    const std::string path = "amc-pass-test.abix";
    auto contract = module_with_layout(8, 8);
    contract.types[0].layout_hash = amc::hash_text("layout", 0x4c41594fULL);
    std::string error;
    check(amc::write_abix(contract, path, error), "artifact written");
    amc::ir::Module loaded;
    check(amc::ir::load_artifact(path, loaded, error), "artifact loaded into ir::Module");
    check(loaded.abi.types.size() == 1 && loaded.abi.types[0].name == "demo::Widget", "ABI view is loaded");
    std::remove(path.c_str());
}

void test_default_pipeline() {
    amc::ir::Module module;
    module.abi = module_with_layout(8, 8);
    module.abi.types[0].layout_hash = amc::hash_text("layout", 0x4c41594fULL);
    amc::diagnostics::DiagnosticList diagnostics;
    check(amc::pass::run_default_pipeline(module, diagnostics), "default pipeline accepts a valid module");
    check(amc::diagnostics::error_count(diagnostics) == 0, "default pipeline has no errors");
}

}   // namespace

int main() {
    test_verify_abi_pass();
    test_compatibility_pass();
    test_artifact_loader();
    test_default_pipeline();

    if (failures != 0) {
        fmt::print(stderr, "amc-pass-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-pass-test: all checks passed\n");
    return 0;
}
