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
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Query/Context.h"
#include "SKL/AMC/Util/Error.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using skl::amc::util::fail;
using skl::amc::util::read_error_category;
using skl::amc::util::usage_error;

namespace skl::amc::cli {
namespace {

llvm::cl::SubCommand context_subcommand("context", "Emit LLM/JSON context for an artifact");
llvm::cl::opt<std::string> context_input(llvm::cl::Positional, llvm::cl::desc("<file.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(context_subcommand));
llvm::cl::opt<std::string> context_format("format", llvm::cl::desc("Output format"), llvm::cl::value_desc("llm|json"),
    llvm::cl::init("llm"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(context_subcommand),
    support::format_callback());
llvm::cl::opt<std::string> context_output("o", llvm::cl::desc("Write output to a file"), llvm::cl::value_desc("file"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(context_subcommand));
llvm::cl::alias context_output_alias("output", llvm::cl::desc("Alias for -o"), llvm::cl::aliasopt(context_output),
    llvm::cl::cat(support::cli_category()));
llvm::cl::opt<bool> context_no_names("no-names", llvm::cl::desc("Omit names from the context"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(context_subcommand));

}   // namespace

int cmd_context() {
    if (context_format != "llm" && context_format != "json") return usage_error("context --format must be llm or json");

    amc::ABIModule module;
    std::string error;
    if (!amc::load_module_source(context_input, module, error))
        return fail(read_error_category(error), "read_abix", error, context_input);

    const bool include_names = !context_no_names;
    const auto emit = [&](llvm::raw_ostream &out) {
        if (context_format == "json")
            amc::context_to_json(module, include_names, out);
        else
            amc::context_to_llm(module, include_names, out);
    };
    if (context_output.empty()) {
        emit(llvm::outs());
        llvm::outs().flush();
        return 0;
    }
    std::error_code ec;
    llvm::raw_fd_ostream file(context_output, ec, llvm::sys::fs::OF_Text);
    if (ec) return fail(amc::ErrorCategory::IO, "context", "cannot write output", context_output, ec.message());
    emit(file);
    file.close();
    if (file.has_error())
        return fail(amc::ErrorCategory::IO, "context", "cannot write output", context_output, file.error().message());
    return 0;
}

}   // namespace skl::amc::cli
