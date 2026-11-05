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

#include <benchmark/benchmark.h>

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

class PassFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State &state) override { count = static_cast<size_t>(state.range(0)); }

    size_t count = 0;
};

}   // namespace

BENCHMARK_DEFINE_F(PassFixture, SemanticVerify)(benchmark::State &state) {
    amc::pass::PipelineOptions options;
    options.verify_abi = false;
    options.lowering = false;
    options.verify_lowering = false;

    amc::diagnostics::DiagnosticList diagnostics;
    for (auto _ : state) {
        state.PauseTiming();
        auto module = make_module(count);
        diagnostics.clear();
        state.ResumeTiming();
        bool ok = amc::pass::run_default_pipeline(module, diagnostics, options);
        benchmark::DoNotOptimize(module);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(PassFixture, SemanticVerify)->Arg(5'000)->Arg(20'000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(PassFixture, AbiVerify)(benchmark::State &state) {
    amc::pass::PipelineOptions options;
    options.verify_semantic = false;
    options.lowering = false;
    options.verify_lowering = false;

    amc::diagnostics::DiagnosticList diagnostics;
    for (auto _ : state) {
        state.PauseTiming();
        auto module = make_module(count);
        diagnostics.clear();
        state.ResumeTiming();
        bool ok = amc::pass::run_default_pipeline(module, diagnostics, options);
        benchmark::DoNotOptimize(module);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(PassFixture, AbiVerify)->Arg(5'000)->Arg(20'000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(PassFixture, Lowering)(benchmark::State &state) {
    amc::pass::PipelineOptions options;
    options.verify_semantic = false;
    options.verify_abi = false;
    options.verify_lowering = false;

    amc::diagnostics::DiagnosticList diagnostics;
    for (auto _ : state) {
        state.PauseTiming();
        auto module = make_module(count);
        diagnostics.clear();
        state.ResumeTiming();
        bool ok = amc::pass::run_default_pipeline(module, diagnostics, options);
        benchmark::DoNotOptimize(module);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(PassFixture, Lowering)->Arg(5'000)->Arg(20'000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(PassFixture, LoweringVerify)(benchmark::State &state) {
    amc::pass::PipelineOptions options;
    options.verify_semantic = false;
    options.verify_abi = false;

    amc::diagnostics::DiagnosticList diagnostics;
    for (auto _ : state) {
        state.PauseTiming();
        auto module = make_module(count);
        diagnostics.clear();
        state.ResumeTiming();
        bool ok = amc::pass::run_default_pipeline(module, diagnostics, options);
        benchmark::DoNotOptimize(module);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(PassFixture, LoweringVerify)->Arg(5'000)->Arg(20'000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(PassFixture, FullPipeline)(benchmark::State &state) {
    amc::diagnostics::DiagnosticList diagnostics;
    for (auto _ : state) {
        state.PauseTiming();
        auto module = make_module(count);
        diagnostics.clear();
        state.ResumeTiming();
        bool ok = amc::pass::run_default_pipeline(module, diagnostics);
        benchmark::DoNotOptimize(module);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(PassFixture, FullPipeline)->Arg(5'000)->Arg(20'000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(PassFixture, Compatibility)(benchmark::State &state) {
    const auto module = make_module(count);
    const auto other = make_module(count);
    amc::ir::CompatibilityResult result;
    std::string error;
    for (auto _ : state) {
        result = {};
        error.clear();
        bool ok = amc::ir::compare_contracts(module.abi, other.abi, result, error);
        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(PassFixture, Compatibility)->Arg(5'000)->Arg(20'000)->Unit(benchmark::kMillisecond);
