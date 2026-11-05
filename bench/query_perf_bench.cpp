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
// B4: Query / context baseline (Google Benchmark).
//
// Query is shared by `amc query`, MCP tools and the LLM context document. The
// fixture keeps output formats honest: exact/substring/hex lookup, text/JSON
// rendering and full context generation are measured separately.
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Query/Context.h"
#include "SKL/AMC/Query/Query.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

amc::AbiModule make_module(size_t count) {
    amc::AbiModule module;
    module.package_name = "query_perf";
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

class QueryFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State &state) override {
        count = static_cast<size_t>(state.range(0));
        module = make_module(count);

        all_types.resize(count);
        for (size_t i = 0; i < count; ++i)
            all_types[i] = i + 1;   // index 0 is int

        all_functions.resize(count);
        for (size_t i = 0; i < count; ++i)
            all_functions[i] = i;

        exact_type = "perf::R" + std::to_string(count / 2);
        substring = "R" + std::to_string(count - 1);
        exact_function = "perf::fn" + std::to_string(count / 2);

        const auto &id = module.types[count / 2].id;
        char buffer[40] = {};
        std::snprintf(buffer, sizeof(buffer), "0X%016llx%016llx", static_cast<unsigned long long>(id.hi),
            static_cast<unsigned long long>(id.lo));
        hex = buffer;
    }

    size_t count = 0;
    amc::AbiModule module;
    std::vector<size_t> all_types;
    std::vector<size_t> all_functions;
    std::string exact_type;
    std::string substring;
    std::string exact_function;
    std::string hex;
};

}   // namespace

BENCHMARK_DEFINE_F(QueryFixture, TypeExact)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_type_indices(module, exact_type);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(QueryFixture, TypeExact)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, TypeSubstring)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_type_indices(module, substring);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(QueryFixture, TypeSubstring)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, TypeHex)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_type_indices(module, hex);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(QueryFixture, TypeHex)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, FunctionExact)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_function_indices(module, exact_function);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(QueryFixture, FunctionExact)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, TypesText)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_types_text(module, all_types, true);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(QueryFixture, TypesText)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, TypesJson)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_types_json(module, all_types, true, "");
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(QueryFixture, TypesJson)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, FunctionsText)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_functions_text(module, all_functions);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(QueryFixture, FunctionsText)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, FunctionsJson)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::query_functions_json(module, all_functions, "");
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(QueryFixture, FunctionsJson)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, ContextLlm)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::context_to_llm(module, true);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(QueryFixture, ContextLlm)->Arg(5'000)->Arg(20'000);

BENCHMARK_DEFINE_F(QueryFixture, ContextJson)(benchmark::State &state) {
    for (auto _ : state) {
        const auto result = amc::context_to_json(module, true);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(count));
}
BENCHMARK_REGISTER_F(QueryFixture, ContextJson)->Arg(5'000)->Arg(20'000);
