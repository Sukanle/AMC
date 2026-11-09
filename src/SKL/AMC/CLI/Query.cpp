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
#include "SKL/AMC/Support/CommandLine.h"
#include "SKL/AMC/CLI/Support.h"
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Query/Query.h"
#include "SKL/AMC/Query/Compatibility.h"
#include "SKL/AMC/Util/Error.h"

#include <llvm/Support/CommandLine.h>

#include <filesystem>
#include <string>

#include <fmt/format.h>

namespace fs = std::filesystem;
using skl::amc::util::fail;
using skl::amc::util::read_error_category;
using skl::amc::util::usage_error;

namespace skl::amc::cli {
namespace {

llvm::cl::SubCommand query_subcommand("query", "Query types, functions or compatibility");
llvm::cl::opt<std::string> query_input(llvm::cl::Positional, llvm::cl::desc("<file.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(query_subcommand));
llvm::cl::opt<std::string> query_type("type", llvm::cl::desc("Match a type name"), llvm::cl::value_desc("name"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(query_subcommand));
llvm::cl::opt<std::string> query_function("function", llvm::cl::desc("Match a function name"),
    llvm::cl::value_desc("name"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(query_subcommand));
llvm::cl::opt<std::string> query_compatible("compatible", llvm::cl::desc("Compare against another artifact"),
    llvm::cl::value_desc("other.abix"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(query_subcommand));
llvm::cl::opt<std::string> query_format("format", llvm::cl::desc("Output format"), llvm::cl::value_desc("text|json"),
    llvm::cl::init("text"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(query_subcommand),
    support::format_callback());
llvm::cl::opt<bool> query_layout("layout", llvm::cl::desc("Include layout details"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(query_subcommand));

llvm::cl::SubCommand inspect_subcommand("inspect", "Summarize a .abix artifact");
llvm::cl::SubCommand validate_subcommand("validate", "Validate a .abix artifact");
llvm::cl::SubCommandGroup artifact_group{&inspect_subcommand, &validate_subcommand};
llvm::cl::opt<std::string> artifact_input(llvm::cl::Positional, llvm::cl::desc("<file.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(artifact_group));

}   // namespace

int cmd_query() {
    const int modes =
        (query_type.empty() ? 0 : 1) + (query_function.empty() ? 0 : 1) + (query_compatible.empty() ? 0 : 1);
    if (modes != 1) return usage_error("query requires exactly one of --type, --function or --compatible");
    if (query_format != "text" && query_format != "json") return usage_error("query --format must be text or json");
    const bool json = query_format == "json";

    amc::ABIModule module;
    std::string error;
    if (!amc::load_module_source(query_input, module, error))
        return fail(read_error_category(error), "read_abix", error, query_input);

    if (!query_type.empty()) {
        const auto indices = amc::query_type_indices(module, query_type);
        fmt::print("{}", json ? amc::query_types_json(module, indices, query_layout, query_type)
                              : amc::query_types_text(module, indices, query_layout));
        return indices.empty() ? 1 : 0;
    }
    if (!query_function.empty()) {
        const auto indices = amc::query_function_indices(module, query_function);
        fmt::print("{}", json ? amc::query_functions_json(module, indices, query_function)
                              : amc::query_functions_text(module, indices));
        return indices.empty() ? 1 : 0;
    }

    amc::ABIModule target;
    if (!amc::load_module_source(query_compatible, target, error))
        return fail(read_error_category(error), "read_abix", error, query_compatible);
    amc::ir::CompatibilityResult result;
    if (!amc::query::compare(module, target, result, error)) return fail(amc::ErrorCategory::INTERNAL, "verify", error);
    fmt::print("{}", json ? amc::compatibility_query_json(module, target, result)
                          : amc::compatibility_query_text(module, target, result));
    return result.consistent ? 0 : 1;
}

int cmd_inspect(bool validate) {
    amc::ABIModule module;
    std::string error;
    if (!amc::load_module_source(artifact_input, module, error))
        return fail(read_error_category(error), "read_abix", error, artifact_input);
    if (!validate)
        fmt::print("package={} types={} fields={} functions={}\n", module.package_name, module.types.size(),
            module.fields.size(), module.functions.size());
    return 0;
}

namespace {
const bool query_registered =
    amc::support::register_command_handler(amc::support::Command::QUERY, [](const fs::path &) { return cmd_query(); });
const bool inspect_registered = amc::support::register_command_handler(
    amc::support::Command::INSPECT, [](const fs::path &) { return cmd_inspect(false); });
const bool validate_registered = amc::support::register_command_handler(
    amc::support::Command::VALIDATE, [](const fs::path &) { return cmd_inspect(true); });
}   // namespace

}   // namespace skl::amc::cli
