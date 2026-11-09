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
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Backend/Lua/Lua.h"
#include "SKL/AMC/Compilation/Config.h"
#include "SKL/AMC/Compilation/Provider.h"
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Support/CommandLine.h"
#include "SKL/AMC/Support/Language.h"
#include "SKL/AMC/Util/Error.h"

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <llvm/Support/CommandLine.h>

#include <fmt/format.h>
#include <fmt/os.h>

namespace fs = std::filesystem;
using skl::amc::support::Language;
using skl::amc::util::fail;
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

using GenerateHandler = int (*)(const fs::path &, const fs::path &, const fs::path &, bool);

int generate_lua(const fs::path &, const fs::path &input, const fs::path &output, bool backend) {
    if (backend) return usage_error("backend is a C++ provider capability; use generate for lua");
    amc::ABIModule module;
    std::string error;
    if (!amc::load_module_source(input, module, error))
        return fail(read_error_category(error), "read_abix", error, input);
    const bool conformance = output.extension() == ".lua";
    const std::string contract =
        conformance ? amc::generate_lua_conformance(module, error) : amc::generate_lua_contract(module, error);
    if (!error.empty()) return fail(amc::ErrorCategory::INTERNAL, "generate_lua", error, input);
    fs::path destination = output;
    if (destination.extension() != ".hpp" && destination.extension() != ".lua") destination /= "amc_lua_contract.hpp";
    std::error_code ec;
    if (!destination.parent_path().empty()) {
        fs::create_directories(destination.parent_path(), ec);
        if (ec)
            return fail(amc::ErrorCategory::IO, "generate", "cannot create output directory", destination.string(),
                ec.message());
    }
    try {
        auto file = fmt::output_file(destination.string());
        file.print("{}", contract);
    } catch (const std::exception &e) {
        return fail(amc::ErrorCategory::IO, "generate", "cannot write output", destination.string(), e.what());
    }
    return 0;
}

int generate_provider(
    Language language, const fs::path &base_provider, const fs::path &input, const fs::path &output, bool backend) {
    const fs::path provider =
        amc::compilation::provider_executable(base_provider, amc::support::language_name(language));
    if (backend) {
        std::string error;
        if (!amc::compilation::dispatch_provider(provider, amc::support::Capability::BACKEND, input, output, error))
            return fail(amc::ErrorCategory::PROVIDER, "backend", error, input);
        return 0;
    }
    const char *extension = language == Language::CPP ? ".hpp" : (language == Language::RUST ? ".rs" : ".zig");
    return amc::compilation::generate(provider, input, output, extension);
}

int generate_cpp(const fs::path &provider, const fs::path &input, const fs::path &output, bool backend) {
    return generate_provider(Language::CPP, provider, input, output, backend);
}

int generate_rust(const fs::path &provider, const fs::path &input, const fs::path &output, bool backend) {
    return generate_provider(Language::RUST, provider, input, output, backend);
}

int generate_zig(const fs::path &provider, const fs::path &input, const fs::path &output, bool backend) {
    return generate_provider(Language::ZIG, provider, input, output, backend);
}

struct LanguageHandler {
    Language language;
    GenerateHandler run;
};

const LanguageHandler kLanguageHandlers[] = {
    { Language::LUA,  generate_lua},
    { Language::CPP,  generate_cpp},
    {Language::RUST, generate_rust},
    { Language::ZIG,  generate_zig},
};

}   // namespace

int cmd_generate(bool backend, const fs::path &provider) {
    const fs::path input = generate_input.getValue();
    const fs::path output = generate_output.getValue();
    if (generate_language.empty() || output.empty())
        return usage_error("generate expects -l cpp|lua|rust|zig -o <directory|file>");

    const Language language = support::language_from(generate_language);
    for (const LanguageHandler &handler : kLanguageHandlers)
        if (handler.language == language) return handler.run(provider, input, output, backend);
    return usage_error("generate expects -l cpp|lua|rust|zig -o <directory|file>");
}

int cmd_frontend(const fs::path &provider) {
    const std::string &language = frontend_language;
    const fs::path config_path = frontend_config.getValue();
    const fs::path output_path = frontend_output.getValue();
    const Language language_kind = support::language_from(language);
    if (language.empty()
        || language_kind == Language::UNKNOWN
        || language_kind == Language::LUA
        || config_path.empty()
        || output_path.empty())
        return usage_error("frontend expects -l cpp|rust|zig -c <config> -o <output>");

    std::string error;
    const fs::path selected = amc::compilation::provider_executable(provider, language);
    if (!amc::compilation::dispatch_provider(
            selected, amc::support::Capability::FRONTEND, config_path, output_path, error))
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

namespace {
const bool generate_registered = amc::support::register_command_handler(amc::support::Command::GENERATE,
    [](const fs::path &executable_dir) { return cmd_generate(false, executable_dir / "amc-cpp"); });
const bool backend_registered = amc::support::register_command_handler(amc::support::Command::BACKEND,
    [](const fs::path &executable_dir) { return cmd_generate(true, executable_dir / "amc-cpp"); });
const bool frontend_registered = amc::support::register_command_handler(amc::support::Command::FRONTEND,
    [](const fs::path &executable_dir) { return cmd_frontend(executable_dir / "amc-cpp"); });
}   // namespace

}   // namespace skl::amc::cli
