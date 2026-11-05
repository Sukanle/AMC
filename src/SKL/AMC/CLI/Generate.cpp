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
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Core/Lua.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Util/Error.h"
#include "SKL/AMC/Compilation/Config.h"
#include "SKL/AMC/Compilation/Provider.h"

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

int cmd_generate(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "generate" || command == "backend") {
        if (arg_count != 7 || std::string(args[3]) != "-l" || std::string(args[5]) != "-o")
            return usage_error(command + " expects -l cpp|lua|rust -o <directory|file>");
        const std::string language = args[4];
        if (language == "lua") {
            if (command == "backend") return usage_error("backend is a C++ provider capability; use generate for lua");
            amc::AbiModule module;
            std::string error;
            if (!amc::load_module_source(args[2], module, error))
                return fail(read_error_category(error), "read_abix", error, args[2]);
            const fs::path requested = args[6];
            const bool conformance = requested.extension() == ".lua";
            const std::string contract =
                conformance ? amc::generate_lua_conformance(module, error) : amc::generate_lua_contract(module, error);
            if (!error.empty()) return fail(amc::ErrorCategory::internal, "generate_lua", error, args[2]);
            fs::path output = requested;
            if (output.extension() != ".hpp" && output.extension() != ".lua") output /= "amc_lua_contract.hpp";
            std::error_code ec;
            if (!output.parent_path().empty()) {
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                    return fail(amc::ErrorCategory::io, "generate", "cannot create output directory", output.string(),
                        ec.message());
            }
            try {
                auto file = fmt::output_file(output.string());
                file.print("{}", contract);
            } catch (const std::exception &e) {
                return fail(amc::ErrorCategory::io, "generate", "cannot write output", output.string(), e.what());
            }
            return 0;
        }
        if (language == "cpp") {
            if (command == "backend") {
                std::string error;
                if (!amc::compilation::dispatch_provider(provider, "backend", args[2], args[6], error))
                    return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
                return 0;
            }
            return amc::compilation::generate(provider, args[2], args[6], ".hpp");
        }
        if (language == "rust") {
            const fs::path rust_provider = amc::compilation::provider_executable(provider, "rust");
            if (command == "backend") {
                std::string error;
                if (!amc::compilation::dispatch_provider(rust_provider, "backend", args[2], args[6], error))
                    return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
                return 0;
            }
            return amc::compilation::generate(rust_provider, args[2], args[6], ".rs");
        }
        if (language == "zig") {
            const fs::path zig_provider = amc::compilation::provider_executable(provider, "zig");
            if (command == "backend") {
                std::string error;
                if (!amc::compilation::dispatch_provider(zig_provider, "backend", args[2], args[6], error))
                    return fail(amc::ErrorCategory::provider, "backend", error, args[2]);
                return 0;
            }
            return amc::compilation::generate(zig_provider, args[2], args[6], ".zig");
        }
        return usage_error(command + " expects -l cpp|lua|rust|zig -o <directory|file>");
    }
    return usage_error(command + ": unsupported subcommand");
}

int cmd_frontend(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "frontend") {
        std::string language;
        fs::path config_path;
        fs::path output_path;
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if (argument == "-l" && i + 1 < arg_count)
                language = args[++i];
            else if (argument == "-c" && i + 1 < arg_count)
                config_path = args[++i];
            else if (argument == "-o" && i + 1 < arg_count)
                output_path = args[++i];
            else
                return usage_error("frontend expects -l <language> -c <config> -o <output>");
        }
        if (language.empty() || config_path.empty() || output_path.empty())
            return usage_error("frontend expects -l <language> -c <config> -o <output>");
        std::string error;
        const fs::path selected = amc::compilation::provider_executable(provider, language);
        if (!amc::compilation::dispatch_provider(selected, "frontend", config_path, output_path, error))
            return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());
        try {
            const toml::table config = toml::parse_file(config_path.string());
            amc::AbiModule module;
            if (!amc::read_abix(output_path.string(), module, error))
                return fail(read_error_category(error), "read_abix", error, output_path.string());
            amc::compilation::apply_target_and_layout(config, module);
            if (!amc::write_abix(module, output_path.string(), error))
                return fail(amc::ErrorCategory::io, "write_abix", error, output_path.string());
        } catch (const toml::parse_error &e) {
            return fail(amc::ErrorCategory::config, "frontend", std::string(e.description()), config_path.string());
        }
        return 0;
    }
    return usage_error(command + ": unsupported subcommand");
}

}   // namespace amc::cli
