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
#include "SKL/AMC/Pass/TypeMapping.h"
#include "SKL/AMC/Core/Cache.h"
#include "SKL/AMC/Core/Context.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/ELF.h"
#include "SKL/AMC/Core/Error.h"
#include "SKL/AMC/Core/Lua.h"
#include "SKL/AMC/Core/Metadata.h"
#include "SKL/AMC/Core/Query.h"
#include "SKL/AMC/Core/Symbol.h"
#include "SKL/AMC/Core/Verify.h"
#include "SKL/AMC/Util/Error.h"

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

namespace fs = std::filesystem;
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

namespace amc::cli {

int cmd_adapter(char **args, int arg_count) {
    const std::string command = args[1];
    if (command == "adapter") {
        fs::path source_path, target_path, output;
        amc::AdapterOptions options;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "-o" || argument == "--output") && i + 1 < arg_count) {
                output = args[++i];
            } else if (argument.rfind("-o=", 0) == 0) {
                output = argument.substr(3);
            } else if (argument.rfind("--output=", 0) == 0) {
                output = argument.substr(std::string("--output=").size());
            } else if (argument == "--typed") {
                options.typed = true;
            } else if (argument == "--shim") {
                options.shim = true;
            } else if (argument == "--typed-namespace" && i + 1 < arg_count) {
                options.typed = true;
                options.typed_namespace = args[++i];
            } else if (argument.rfind("--typed-namespace=", 0) == 0) {
                options.typed = true;
                options.typed_namespace = argument.substr(std::string("--typed-namespace=").size());
            } else if (source_path.empty()) {
                source_path = argument;
            } else if (target_path.empty()) {
                target_path = argument;
            } else {
                return usage_error("adapter accepts <source.abix> <target.abix>");
            }
        }
        if (source_path.empty() || target_path.empty())
            return usage_error("adapter requires <source.abix> <target.abix> [-o <file>]");

        amc::AbiModule source, target, report;
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
            return fail(amc::ErrorCategory::compatibility, "build_compatibility", message);
        }
        const std::string text = amc::generate_adapter(source, target, report, options, error);
        if (!error.empty()) return fail(amc::ErrorCategory::compatibility, "adapter", error, source_path.string());
        if (output.empty()) {
            fmt::print("{}", text);
            return 0;
        }
        std::error_code ec;
        if (!output.parent_path().empty()) {
            fs::create_directories(output.parent_path(), ec);
            if (ec)
                return fail(
                    amc::ErrorCategory::io, "adapter", "cannot create output directory", output.string(), ec.message());
        }
        try {
            auto file = fmt::output_file(output.string());
            file.print("{}", text);
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::io, "adapter", "cannot write output", output.string(), e.what());
        }
        return 0;
    }
    return usage_error(command + ": unsupported subcommand");
}

}   // namespace amc::cli
