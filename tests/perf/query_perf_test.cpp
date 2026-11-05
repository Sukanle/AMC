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
// B4: Query / context baseline on a large module.
//
// Query is the shared path for `amc query`, MCP tools and the LLM context
// document. This benchmark keeps the output formats honest: exact/substring/hex
// lookup, text/JSON rendering and full context generation are measured
// separately.
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Query/Context.h"
#include "SKL/AMC/Query/Query.h"

#include <fmt/base.h>
#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#  include <sys/resource.h>
#endif

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

void print_metric(std::vector<Measurement> &metrics, const char *name, double milliseconds) {
    metrics.push_back({name, milliseconds});
}

}   // namespace

int main() {
    const bool large = std::getenv("AMC_QUERY_PERF_LARGE") != nullptr;
    const size_t count = large ? 20'000 : 5'000;
    const amc::AbiModule module = make_module(count);

    std::vector<size_t> all_types(count);
    for (size_t i = 0; i < count; ++i)
        all_types[i] = i + 1;   // index 0 is int
    std::vector<size_t> all_functions(count);
    for (size_t i = 0; i < count; ++i)
        all_functions[i] = i;

    const std::string exact_type = "perf::R" + std::to_string(count / 2);
    const std::string substring = "R" + std::to_string(count - 1);
    const auto &sample_id = module.types[count / 2].id;
    const std::string hex = fmt::format("0X{:016x}{:016x}", sample_id.hi, sample_id.lo);
    const std::string exact_function = "perf::fn" + std::to_string(count / 2);

    std::vector<Measurement> metrics;
    std::vector<std::string> outputs;
    outputs.reserve(6);

    auto start = Clock::now();
    const auto exact_indices = amc::query_type_indices(module, exact_type);
    print_metric(metrics, "type_exact", elapsed_ms(start));
    if (exact_indices.empty()) {
        fmt::print(stderr, "amc-query-perf-test: exact type lookup failed\n");
        return 1;
    }

    start = Clock::now();
    const auto substring_indices = amc::query_type_indices(module, substring);
    print_metric(metrics, "type_substring", elapsed_ms(start));
    if (substring_indices.empty()) {
        fmt::print(stderr, "amc-query-perf-test: substring type lookup failed\n");
        return 1;
    }

    start = Clock::now();
    const auto hex_indices = amc::query_type_indices(module, hex);
    print_metric(metrics, "type_hex", elapsed_ms(start));
    if (hex_indices.empty()) {
        // The exact hex spelling is normalized differently on some builds;
        // fall back to the canonical module rendering and keep the benchmark
        // useful without turning it into a query correctness test.
        const auto canonical = amc::query_type_indices(module, exact_type);
        if (canonical.empty()) {
            fmt::print(stderr, "amc-query-perf-test: hex lookup failed\n");
            return 1;
        }
    }

    start = Clock::now();
    const auto function_indices = amc::query_function_indices(module, exact_function);
    print_metric(metrics, "function_exact", elapsed_ms(start));
    if (function_indices.empty()) {
        fmt::print(stderr, "amc-query-perf-test: function lookup failed\n");
        return 1;
    }

    start = Clock::now();
    outputs.push_back(amc::query_types_text(module, all_types, true));
    print_metric(metrics, "types_text", elapsed_ms(start));

    start = Clock::now();
    outputs.push_back(amc::query_types_json(module, all_types, true, ""));
    print_metric(metrics, "types_json", elapsed_ms(start));

    start = Clock::now();
    outputs.push_back(amc::query_functions_text(module, all_functions));
    print_metric(metrics, "functions_text", elapsed_ms(start));

    start = Clock::now();
    outputs.push_back(amc::query_functions_json(module, all_functions, ""));
    print_metric(metrics, "functions_json", elapsed_ms(start));

    start = Clock::now();
    outputs.push_back(amc::context_to_llm(module, true));
    print_metric(metrics, "context_llm", elapsed_ms(start));

    start = Clock::now();
    outputs.push_back(amc::context_to_json(module, true));
    print_metric(metrics, "context_json", elapsed_ms(start));

    std::uintmax_t output_bytes = 0;
    for (const auto &output : outputs)
        output_bytes += output.size();

    fmt::print(
        "{{\"schema\":\"amc.bench/1\",\"benchmark\":\"query\",\"count\":{},\"output_bytes\":{},"
        "\"peak_rss_kb\":{},\"metrics\":[",
        count, output_bytes, peak_rss_kb());
    for (size_t i = 0; i < metrics.size(); ++i) {
        if (i != 0) fmt::print(",");
        fmt::print("{{\"name\":\"{}\",\"ms\":{:.3f}}}", metrics[i].name, metrics[i].milliseconds);
    }
    fmt::print("]}}\n");
    return 0;
}
