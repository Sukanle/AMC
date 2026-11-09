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
#include "SKL/AMC/Backend/Backend.h"
#include "SKL/AMC/Compilation/Compilation.h"
#include "SKL/AMC/Frontend/Frontend.h"
#include "SKL/AMC/IR/Module.h"
#include "SKL/AMC/Pass/Pass.h"

#include <fmt/base.h>

#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

void test_ir_contract() {
    skl::amc::ir::Module module;
    check(module.schema_version == skl::amc::ir::module_schema_version, "IR module carries its schema version");
    check(module.abi.types.empty(), "IR module starts with an empty ABI view");
    check(module.semantic.types.empty(), "IR module starts with an empty semantic view");
}

void test_frontend_contract() {
    skl::amc::frontend::Request request;
    request.language = "cpp";
    request.config_path = "api.abic.toml";

    skl::amc::frontend::Frontend frontend;
    frontend.language = "cpp";
    frontend.run = [](const skl::amc::frontend::Request &input) {
        skl::amc::frontend::Result result;
        result.module.abi.package_name = input.language;
        result.dependencies.push_back(input.config_path);
        return result;
    };

    const auto result = frontend.run(request);
    check(result.ok(), "frontend result without diagnostics is successful");
    check(result.module.abi.package_name == "cpp", "frontend fills the IR");
    check(result.dependencies.size() == 1, "frontend reports its dependency closure");
}

void test_pass_contract() {
    skl::amc::ir::Module module;
    skl::amc::pass::Context context;
    skl::amc::diagnostics::DiagnosticList diagnostics;
    std::vector<std::string> order;

    skl::amc::pass::Pipeline pipeline;
    pipeline.passes.push_back(
        {"first", [&](skl::amc::ir::Module &, skl::amc::pass::Context &, skl::amc::diagnostics::DiagnosticList &out) {
             order.push_back("first");
             skl::amc::diagnostics::add_warning(out, "demo_warning", "pass", "example warning");
             return true;
         }});
    pipeline.passes.push_back(
        {"second", [&](skl::amc::ir::Module &, skl::amc::pass::Context &, skl::amc::diagnostics::DiagnosticList &out) {
             order.push_back("second");
             out.push_back({skl::amc::diagnostics::Severity::NOTE, "demo_note", "pass", "example note", {}, 0, 0});
             return true;
         }});

    check(pipeline.run(module, context, diagnostics), "pass pipeline runs in order");
    check(order.size() == 2 && order[0] == "first" && order[1] == "second", "pass order is preserved");
    check(!skl::amc::diagnostics::has_errors(diagnostics), "warnings and notes do not fail the pipeline");

    skl::amc::diagnostics::DiagnosticList failed;
    skl::amc::pass::Pipeline failing;
    failing.passes.push_back({"broken", [](skl::amc::ir::Module &, skl::amc::pass::Context &,
                                            skl::amc::diagnostics::DiagnosticList &) { return false; }});
    check(!failing.run(module, context, failed), "a failing pass stops the pipeline");
    check(skl::amc::diagnostics::has_errors(failed), "a failing pass produces a diagnostic");
    check(!failed.empty() && failed[0].code == "pass_failed", "missing pass diagnostics get a stable code");
}

void test_backend_contract() {
    skl::amc::ir::Module module;
    skl::amc::backend::Request request;
    request.module = &module;
    request.language = "rust";
    request.output_directory = "out";

    skl::amc::backend::Result result;
    result.artifacts.push_back({
        "generated.rs", "out/generated.rs", {1, 2, 3}
    });
    check(result.ok(), "backend result without diagnostics is successful");
    check(result.artifacts.size() == 1 && result.artifacts[0].bytes.size() == 3, "backend emits artifacts");
    check(request.module == &module, "backend request borrows the IR without owning it");
}

void test_compilation_contract() {
    skl::amc::compilation::Request request;
    request.config_path = "api.abic.toml";
    request.build_directory = "build";

    skl::amc::compilation::Result result;
    result.module.abi.package_name = "api";
    check(result.ok(), "compilation result without diagnostics is successful");
    check(result.module.abi.package_name == "api", "compilation exposes the compiled IR");
}

}   // namespace

int main() {
    test_ir_contract();
    test_frontend_contract();
    test_pass_contract();
    test_backend_contract();
    test_compilation_contract();

    if (failures != 0) {
        fmt::print(stderr, "amc-boundary-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-boundary-test: all checks passed\n");
    return 0;
}
