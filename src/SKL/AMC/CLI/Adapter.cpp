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
#include "SKL/AMC/Pass/TypeMapping.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Util/Error.h"

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <llvm/Support/CommandLine.h>

#include <fmt/format.h>
#include <fmt/os.h>

namespace fs = std::filesystem;
using skl::amc::util::fail;
using skl::amc::util::g_error_format;
using skl::amc::util::read_error_category;
using skl::amc::util::usage_error;

namespace skl::amc::cli {
namespace {

llvm::cl::SubCommand adapter_subcommand("adapter", "Generate an adapter between two artifacts");
llvm::cl::opt<std::string> adapter_source(llvm::cl::Positional, llvm::cl::desc("<source.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(adapter_subcommand));
llvm::cl::opt<std::string> adapter_target(llvm::cl::Positional, llvm::cl::desc("<target.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(adapter_subcommand));
llvm::cl::opt<std::string> adapter_output("o", llvm::cl::desc("Write output to a file"), llvm::cl::value_desc("file"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(adapter_subcommand));
llvm::cl::alias adapter_output_alias("output", llvm::cl::desc("Alias for -o"), llvm::cl::aliasopt(adapter_output),
    llvm::cl::cat(support::cli_category()));
llvm::cl::opt<bool> adapter_typed("typed", llvm::cl::desc("Emit typed adapters"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(adapter_subcommand));
llvm::cl::opt<bool> adapter_shim(
    "shim", llvm::cl::desc("Emit a shim"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(adapter_subcommand));
llvm::cl::opt<std::string> adapter_typed_namespace("typed-namespace", llvm::cl::desc("Typed adapter namespace"),
    llvm::cl::value_desc("name"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(adapter_subcommand));

}   // namespace

int cmd_adapter() {
    const fs::path source_path = adapter_source.getValue();
    const fs::path target_path = adapter_target.getValue();
    const fs::path output = adapter_output.getValue();
    amc::AdapterOptions options;
    options.typed = adapter_typed || !adapter_typed_namespace.empty();
    options.shim = adapter_shim;
    options.typed_namespace = adapter_typed_namespace;
    {
        amc::ABIModule source, target, report;
        std::string error;
        if (!amc::load_module_source(source_path.string(), source, error))
            return fail(read_error_category(error), "read_abix", error, source_path.string());
        if (!amc::load_module_source(target_path.string(), target, error))
            return fail(read_error_category(error), "read_abix", error, target_path.string());
        amc::pass::Context context;
        context.strict = false;
        amc::diagnostics::DiagnosticList diagnostics;
        if (!amc::pass::build_compatibility_report(source, target, report, context, diagnostics)) {
            const std::string message = diagnostics.empty() ? error : diagnostics.front().message;
            return fail(amc::ErrorCategory::COMPATIBILITY, "build_compatibility", message);
        }
        const std::string text = amc::generate_adapter(source, target, report, options, error);
        if (!error.empty()) return fail(amc::ErrorCategory::COMPATIBILITY, "adapter", error, source_path.string());
        if (output.empty()) {
            fmt::print("{}", text);
            return 0;
        }
        std::error_code ec;
        if (!output.parent_path().empty()) {
            fs::create_directories(output.parent_path(), ec);
            if (ec)
                return fail(
                    amc::ErrorCategory::IO, "adapter", "cannot create output directory", output.string(), ec.message());
        }
        try {
            auto file = fmt::output_file(output.string());
            file.print("{}", text);
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::IO, "adapter", "cannot write output", output.string(), e.what());
        }
        return 0;
    }
}

namespace {
const bool adapter_registered = amc::support::register_command_handler(
    amc::support::Command::ADAPTER, [](const fs::path &) { return cmd_adapter(); });
}   // namespace

}   // namespace skl::amc::cli
