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
#include "SKL/AMC/Backend/Lua/Lua.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Util/Error.h"
#include "SKL/AMC/Compilation/Config.h"
#include "SKL/AMC/Compilation/Provider.h"

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

llvm::cl::SubCommand generate_subcommand("generate", "Project an artifact to a language backend");
llvm::cl::SubCommand backend_subcommand("backend", "Run a language backend directly");
llvm::cl::SubCommandGroup generate_group{&generate_subcommand, &backend_subcommand};
llvm::cl::opt<std::string> generate_input(llvm::cl::Positional, llvm::cl::desc("<file.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(generate_group));
llvm::cl::opt<std::string> generate_language("l", llvm::cl::desc("Projection language"),
    llvm::cl::value_desc("cpp|lua|rust|zig"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(generate_group));
llvm::cl::opt<std::string> generate_output("o", llvm::cl::desc("Output directory or file"),
    llvm::cl::value_desc("path"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(generate_group));

llvm::cl::SubCommand frontend_subcommand("frontend", "Run one language frontend");
llvm::cl::opt<std::string> frontend_language("l", llvm::cl::desc("Source language"),
    llvm::cl::value_desc("cpp|rust|zig"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(frontend_subcommand));
llvm::cl::opt<std::string> frontend_config("c", llvm::cl::desc("Frontend configuration"),
    llvm::cl::value_desc("file.abic.toml"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(frontend_subcommand));
llvm::cl::opt<std::string> frontend_output("o", llvm::cl::desc("Output .abix artifact"),
    llvm::cl::value_desc("file.abix"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(frontend_subcommand));

}   // namespace

int cmd_generate(bool backend, const fs::path &provider) {
    const fs::path input = generate_input.getValue();
    const std::string &language = generate_language;
    const fs::path output = generate_output.getValue();
    if (language.empty() || output.empty()) {
        return usage_error("generate expects -l cpp|lua|rust|zig -o <directory|file>");
    }
    {
        if (language == "lua") {
            if (backend) return usage_error("backend is a C++ provider capability; use generate for lua");
            amc::ABIModule module;
            std::string error;
            if (!amc::load_module_source(input, module, error))
                return fail(read_error_category(error), "read_abix", error, input);
            const fs::path requested = output;
            const bool conformance = requested.extension() == ".lua";
            const std::string contract =
                conformance ? amc::generate_lua_conformance(module, error) : amc::generate_lua_contract(module, error);
            if (!error.empty()) return fail(amc::ErrorCategory::INTERNAL, "generate_lua", error, input);
            fs::path output = requested;
            if (output.extension() != ".hpp" && output.extension() != ".lua") output /= "amc_lua_contract.hpp";
            std::error_code ec;
            if (!output.parent_path().empty()) {
                fs::create_directories(output.parent_path(), ec);
                if (ec)
                    return fail(amc::ErrorCategory::IO, "generate", "cannot create output directory", output.string(),
                        ec.message());
            }
            try {
                auto file = fmt::output_file(output.string());
                file.print("{}", contract);
            } catch (const std::exception &e) {
                return fail(amc::ErrorCategory::IO, "generate", "cannot write output", output.string(), e.what());
            }
            return 0;
        }
        if (language == "cpp") {
            if (backend) {
                std::string error;
                if (!amc::compilation::dispatch_provider(provider, "backend", input, output, error))
                    return fail(amc::ErrorCategory::PROVIDER, "backend", error, input);
                return 0;
            }
            return amc::compilation::generate(provider, input, output, ".hpp");
        }
        if (language == "rust") {
            const fs::path rust_provider = amc::compilation::provider_executable(provider, "rust");
            if (backend) {
                std::string error;
                if (!amc::compilation::dispatch_provider(rust_provider, "backend", input, output, error))
                    return fail(amc::ErrorCategory::PROVIDER, "backend", error, input);
                return 0;
            }
            return amc::compilation::generate(rust_provider, input, output, ".rs");
        }
        if (language == "zig") {
            const fs::path zig_provider = amc::compilation::provider_executable(provider, "zig");
            if (backend) {
                std::string error;
                if (!amc::compilation::dispatch_provider(zig_provider, "backend", input, output, error))
                    return fail(amc::ErrorCategory::PROVIDER, "backend", error, input);
                return 0;
            }
            return amc::compilation::generate(zig_provider, input, output, ".zig");
        }
        return usage_error("generate expects -l cpp|lua|rust|zig -o <directory|file>");
    }
}

int cmd_frontend(const fs::path &provider) {
    const std::string &language = frontend_language;
    const fs::path config_path = frontend_config.getValue();
    const fs::path output_path = frontend_output.getValue();
    if (language.empty() || config_path.empty() || output_path.empty())
        return usage_error("frontend expects -l <language> -c <config> -o <output>");
    {
        std::string error;
        const fs::path selected = amc::compilation::provider_executable(provider, language);
        if (!amc::compilation::dispatch_provider(selected, "frontend", config_path, output_path, error))
            return fail(amc::ErrorCategory::PROVIDER, "frontend", error, config_path.string());
        try {
            const toml::table config = toml::parse_file(config_path.string());
            amc::ABIModule module;
            if (!amc::read_abix(output_path.string(), module, error))
                return fail(read_error_category(error), "read_abix", error, output_path.string());
            amc::compilation::apply_target_and_layout(config, module);
            if (!amc::write_abix(module, output_path.string(), error))
                return fail(amc::ErrorCategory::IO, "write_abix", error, output_path.string());
        } catch (const toml::parse_error &e) {
            return fail(amc::ErrorCategory::CONFIG, "frontend", std::string(e.description()), config_path.string());
        }
        return 0;
    }
}

namespace {
const bool generate_registered = amc::support::register_command_handler(amc::support::Command::GENERATE,
    [](const fs::path &executable_dir) { return cmd_generate(false, executable_dir / "amc-cpp"); });
const bool backend_registered = amc::support::register_command_handler(amc::support::Command::BACKEND,
    [](const fs::path &executable_dir) { return cmd_generate(true, executable_dir / "amc-cpp"); });
const bool frontend_registered = amc::support::register_command_handler(amc::support::Command::FRONTEND,
    [](const fs::path &executable_dir) { return cmd_frontend(executable_dir / "amc-cpp"); });
}   // namespace

}   // namespace skl::amc::cli
