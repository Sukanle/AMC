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
llvm::cl::opt<std::string> error_format_option("error-format", llvm::cl::desc("Structured error envelope"),
    llvm::cl::value_desc("text|json"), llvm::cl::init("text"), llvm::cl::cat(support::cli_category()),
    llvm::cl::sub(llvm::cl::SubCommand::getAll()), support::error_format_callback());

// `build_command` lives in the amc-compilation static library; registering it
// here keeps the object file linked now that the dispatcher has no switch.
const bool build_command_registered = amc::support::register_command_handler(amc::support::Command::BUILD,
    [](const fs::path &executable_dir) { return amc::compilation::build_command(executable_dir / "amc-cpp"); });

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    text.erase(0, first);
    return text;
}

}   // namespace

bool parse_command_line(int argc, char **argv, std::string &error) {
    llvm::cl::HideUnrelatedOptions(support::cli_category());
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream &out) { out << "amc " AMC_VERSION "\n"; });

    llvm::raw_string_ostream stream(error);
    const bool ok = llvm::cl::ParseCommandLineOptions(argc, argv, "AMC interface compiler", &stream);
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
    std::string parse_error;
    const bool parsed = parse_command_line(argc, argv, parse_error);
    if (g_error_format_invalid) return usage_error("--error-format must be text or json");
    if (!parsed) {
        const std::string message = trim(parse_error);
        // Some LLVM value errors are printed directly to stderr and leave the
        // provided stream empty; do not print an empty second envelope.
        if (message.empty()) return 2;
        return usage_error(message);
    }
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

    if (const CommandHandler handler = command_handler(command)) return handler(executable_dir);

    // No command selected: let LLVM render the subcommand/option overview.
    llvm::cl::PrintHelpMessage(false, true);
    return 2;
}

}   // namespace skl::amc::cli
