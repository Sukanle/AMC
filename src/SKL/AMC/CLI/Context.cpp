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
#include "SKL/AMC/CLI/Support.h"
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Query/Context.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Util/Error.h"

#include <filesystem>
#include <string>
#include <vector>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

namespace fs = std::filesystem;
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

namespace amc::cli {

int cmd_context(char **args, int arg_count) {
    const std::string command = args[1];
    if (command == "context") {
        fs::path input;
        fs::path output;
        std::string format = "llm";
        bool include_names = true;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "--format" || argument == "-o") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "--format")
                    format = value;
                else
                    output = value;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else if (argument == "--no-names") {
                include_names = false;
            } else if (input.empty()) {
                input = argument;
            } else {
                return usage_error("context accepts a single .abix input");
            }
        }
        if (input.empty()) return usage_error("context requires an input .abix file");
        if (format != "llm" && format != "json") return usage_error("context --format must be llm or json");

        amc::AbiModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        const auto emit = [&](llvm::raw_ostream &out) {
            if (format == "json")
                amc::context_to_json(module, include_names, out);
            else
                amc::context_to_llm(module, include_names, out);
        };
        if (output.empty()) {
            emit(llvm::outs());
            llvm::outs().flush();
            return 0;
        }
        std::error_code ec;
        llvm::raw_fd_ostream file(output.string(), ec, llvm::sys::fs::OF_Text);
        if (ec) return fail(amc::ErrorCategory::io, "context", "cannot write output", output.string(), ec.message());
        emit(file);
        file.close();
        if (file.has_error())
            return fail(
                amc::ErrorCategory::io, "context", "cannot write output", output.string(), file.error().message());
        return 0;
    }
    return usage_error(command + ": unsupported subcommand");
}

}   // namespace amc::cli
