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
#include "SKL/AMC/CLI/CLI.h"

#include "SKL/AMC/CLI/CommandLine.h"
#include "SKL/AMC/CLI/Reflect.h"
#include "SKL/AMC/Compilation/Build.h"
#include "SKL/AMC/Compilation/Provider.h"
#include "SKL/AMC/Support/CommandLine.h"

#include <llvm/Support/raw_ostream.h>

#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#ifndef AMC_VERSION
#  define AMC_VERSION "0.0.0"
#endif

namespace skl::amc::cli {

namespace {

llvm::cl::opt<bool> list_languages(
    "list-languages", llvm::cl::desc("List the installed language providers"), llvm::cl::cat(support::cli_category()));
llvm::cl::opt<std::string> describe_language("describe-language",
    llvm::cl::desc("Describe one installed language provider"), llvm::cl::value_desc("language"),
    llvm::cl::cat(support::cli_category()));
// Registered for `--help` only: `--error-format` is consumed before parsing so
// parse failures already use the selected envelope.
llvm::cl::opt<std::string> error_format_option("error-format", llvm::cl::desc("Structured error envelope"),
    llvm::cl::value_desc("text|json"), llvm::cl::init("text"), llvm::cl::cat(support::cli_category()),
    llvm::cl::sub(llvm::cl::SubCommand::getAll()));

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    text.erase(0, first);
    return text;
}

}   // namespace

bool parse_command_line(const std::vector<std::string> &arguments, std::string &error) {
    llvm::cl::HideUnrelatedOptions(support::cli_category());
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream &out) { out << "amc " AMC_VERSION "\n"; });

    std::vector<const char *> pointers;
    pointers.reserve(arguments.size());
    for (const std::string &argument : arguments)
        pointers.push_back(argument.c_str());

    llvm::raw_string_ostream stream(error);
    const bool ok = llvm::cl::ParseCommandLineOptions(
        static_cast<int>(pointers.size()), pointers.data(), "AMC interface compiler", &stream);
    stream.flush();
    return ok;
}

Command selected_command() {
    for (llvm::cl::SubCommand *subcommand : llvm::cl::getRegisteredSubcommands()) {
        if (!*subcommand) continue;
        const llvm::StringRef name = subcommand->getName();
        const Command command =
            amc::reflect::enum_value<Command>(std::string_view(name.data(), name.size()), Command::NONE);
        if (command != Command::NONE) return command;
    }
    return Command::NONE;
}

int run(int argc, char **argv, const fs::path &executable_dir) {
    // --error-format selects the envelope used for every later failure, so it
    // is resolved before LLVM can report a parse error.
    std::vector<std::string> filtered;
    filtered.emplace_back(argc > 0 ? argv[0] : "amc");
    bool explicit_error_format = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--error-format" || argument == "-error-format") {
            if (i + 1 >= argc) return usage_error("--error-format requires a value");
            explicit_error_format = true;
            if (!amc::parse_error_format(argv[++i], g_error_format))
                return usage_error("--error-format must be text or json");
            continue;
        }
        if (argument.rfind("--error-format=", 0) == 0 || argument.rfind("-error-format=", 0) == 0) {
            explicit_error_format = true;
            const size_t prefix = argument.find('=') + 1;
            if (!amc::parse_error_format(argument.substr(prefix), g_error_format))
                return usage_error("--error-format must be text or json");
            continue;
        }
        filtered.push_back(argument);
    }
    if (!explicit_error_format) {
        for (size_t i = 1; i < filtered.size(); ++i) {
            const bool format_option = filtered[i] == "--format" || filtered[i] == "-format";
            if (filtered[i] == "--format=json"
                || filtered[i] == "-format=json"
                || (format_option && i + 1 < filtered.size() && filtered[i + 1] == "json")) {
                g_error_format = amc::ErrorFormat::JSON;
                break;
            }
        }
    }

    std::string parse_error;
    if (!parse_command_line(filtered, parse_error)) return usage_error(trim(parse_error));
    const Command command = selected_command();

    if (list_languages) {
        amc::compilation::ProviderManifest manifest;
        std::string manifest_error;
        if (!amc::compilation::load_provider_manifest(executable_dir, manifest, manifest_error))
            return fail(amc::ErrorCategory::CONFIG, "provider_manifest", manifest_error);
        for (const auto &entry : manifest.providers)
            fmt::print("{}\n", entry.language);
        return 0;
    }
    if (!describe_language.empty()) {
        amc::compilation::ProviderManifest manifest;
        std::string manifest_error;
        if (!amc::compilation::load_provider_manifest(executable_dir, manifest, manifest_error))
            return fail(amc::ErrorCategory::CONFIG, "provider_manifest", manifest_error);
        if (const auto *entry = amc::compilation::find_provider_manifest(manifest, describe_language)) {
            fmt::print("{}\n", amc::compilation::describe_provider(*entry));
            return 0;
        }
        std::string supported;
        for (const auto &entry : manifest.providers) {
            if (!supported.empty()) supported += ", ";
            supported += entry.language;
        }
        return usage_error("--describe-language expects one of: " + supported);
    }

    const fs::path provider = executable_dir / "amc-cpp";
    switch (command) {
        case Command::BUILD:         return amc::compilation::build_command(provider);
        case Command::CONTEXT:       return cmd_context();
        case Command::METADATA:      return cmd_metadata();
        case Command::ADAPTER:       return cmd_adapter();
        case Command::QUERY:         return cmd_query();
        case Command::INSPECT:
        case Command::VALIDATE:      return cmd_inspect(command == Command::VALIDATE);
        case Command::PUBLISH:
        case Command::FETCH:         return cmd_symbols(command == Command::PUBLISH);
        case Command::VERIFY:        return cmd_verify(provider);
        case Command::TARGET:        return cmd_target();
        case Command::DIFF:
        case Command::COMPATIBILITY: return cmd_diff(command == Command::COMPATIBILITY);
        case Command::CACHE:         return cmd_cache();
        case Command::GENERATE:
        case Command::BACKEND:       return cmd_generate(command == Command::BACKEND, provider);
        case Command::FRONTEND:      return cmd_frontend(provider);
        case Command::NONE:          break;
    }
    // No command selected: let LLVM render the subcommand/option overview.
    llvm::cl::PrintHelpMessage(false, true);
    return 2;
}

}   // namespace skl::amc::cli
