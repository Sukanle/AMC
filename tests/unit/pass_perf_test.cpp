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
#include "SKL/AMC/IR/Compatibility.h"
#include "SKL/AMC/Pass/Pipeline.h"

#include <fmt/base.h>

#include <chrono>
#include <cstdlib>
#include <string>

namespace {

amc::ir::Module make_module(size_t count) {
    amc::ir::Module module;
    module.abi.types.reserve(count);
    module.semantic.types.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = "perf::T" + std::to_string(i);
        amc::Type type;
        type.name = name;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(name, 0x54595045ULL);
        type.size = 8;
        type.align = 8;
        type.layout_hash = amc::hash_text(name + ":layout", 0x4c41594fULL);
        module.abi.types.push_back(std::move(type));

        amc::semantic::SemanticType semantic;
        semantic.id = amc::semantic::SemanticTypeId{amc::hash_text(name, 0x53455459ULL)};
        semantic.kind = amc::semantic::SemanticTypeKind::record;
        semantic.qualified_name = name;
        semantic.abi_type = module.abi.types.back().id;
        module.semantic.types.push_back(std::move(semantic));
    }

    module.abi.functions.reserve(count);
    module.semantic.functions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = "perf::fn" + std::to_string(i);
        const size_t type_index = i % count;

        amc::Function function;
        function.name = name;
        function.signature = amc::hash_text(name + ":signature", 0x5349474eULL);
        function.return_type = module.abi.types[type_index].id;
        amc::Parameter parameter;
        parameter.name = "value";
        parameter.type_id = module.abi.types[type_index].id;
        function.parameters.push_back(std::move(parameter));
        module.abi.functions.push_back(std::move(function));

        amc::semantic::SemanticFunction semantic;
        semantic.id = amc::semantic::SemanticFunctionId{amc::hash_text(name, 0x53454e46ULL)};
        semantic.kind = amc::semantic::SemanticFunctionKind::free_function;
        semantic.qualified_name = name;
        semantic.unqualified_name = "fn" + std::to_string(i);
        semantic.return_type = module.semantic.types[type_index].id;
        semantic.abi_signature = amc::hash_text(name + ":signature", 0x5349474eULL);
        amc::semantic::SemanticParameter semantic_parameter;
        semantic_parameter.name = "value";
        semantic_parameter.type = module.semantic.types[type_index].id;
        semantic.parameters.push_back(std::move(semantic_parameter));
        module.semantic.functions.push_back(std::move(semantic));
    }
    return module;
}

long long elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

bool run_stage(const char *label, size_t count, const amc::pass::PipelineOptions &options) {
    auto module = make_module(count);
    amc::diagnostics::DiagnosticList diagnostics;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = amc::pass::run_default_pipeline(module, diagnostics, options);
    fmt::print("amc-pass-perf-test: {:<16} {} types/functions, {} diagnostics, {} ms\n", label, count,
        diagnostics.size(), elapsed_ms(start));
    if (!ok) {
        fmt::print(stderr, "amc-pass-perf-test: {} pipeline failed: {}\n", label,
            diagnostics.empty() ? "<no diagnostic>" : diagnostics.front().message);
    }
    return ok;
}

}   // namespace

int main() {
    constexpr size_t kCount = 5'000;
    const bool large = std::getenv("AMC_PASS_PERF_LARGE") != nullptr;
    const size_t count = large ? 20'000 : kCount;

    amc::pass::PipelineOptions semantic_options;
    semantic_options.verify_abi = false;
    semantic_options.lowering = false;
    semantic_options.verify_lowering = false;

    amc::pass::PipelineOptions abi_options;
    abi_options.verify_semantic = false;
    abi_options.lowering = false;
    abi_options.verify_lowering = false;

    amc::pass::PipelineOptions lowering_options;
    lowering_options.verify_semantic = false;
    lowering_options.verify_abi = false;
    lowering_options.verify_lowering = false;

    amc::pass::PipelineOptions lower_verify_options;
    lower_verify_options.verify_semantic = false;
    lower_verify_options.verify_abi = false;

    bool ok = run_stage("semantic-verify", count, semantic_options);
    ok = run_stage("abi-verify", count, abi_options) && ok;
    ok = run_stage("lowering", count, lowering_options) && ok;
    ok = run_stage("lowering+verify", count, lower_verify_options) && ok;

    {
        auto module = make_module(count);
        amc::diagnostics::DiagnosticList diagnostics;
        const auto start = std::chrono::steady_clock::now();
        ok = amc::pass::run_default_pipeline(module, diagnostics) && ok;
        fmt::print("amc-pass-perf-test: {:<16} {} types/functions, {} diagnostics, {} ms\n", "full-pipeline", count,
            diagnostics.size(), elapsed_ms(start));
        if (!ok) {
            fmt::print(stderr, "amc-pass-perf-test: full pipeline failed: {}\n",
                diagnostics.empty() ? "<no diagnostic>" : diagnostics.front().message);
        }
    }

    {
        auto module = make_module(count);
        auto other = make_module(count);
        amc::ir::CompatibilityResult result;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        const bool compared = amc::ir::compare_contracts(module.abi, other.abi, result, error);
        fmt::print("amc-pass-perf-test: {:<16} {} types/functions, consistent={}, {} ms\n", "compatibility", count,
            result.consistent, elapsed_ms(start));
        if (!compared || !result.consistent) {
            fmt::print(stderr, "amc-pass-perf-test: compatibility comparison failed: {}\n", error);
            ok = false;
        }
    }

    if (!ok) return 1;
    fmt::print("amc-pass-perf-test: baseline ok (set AMC_PASS_PERF_LARGE=1 for 20k)\n");
    return 0;
}
