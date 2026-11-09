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
#include "SKL/AMC/Compilation/Build.h"
#include "SKL/AMC/Compilation/Provider.h"

#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

namespace {

// Resolve the directory that contains the running `amc` binary. `argv[0]` can
// be a bare name when the tool is invoked through PATH; `fs::absolute(argv[0])`
// would then point at the current directory and provider lookup would fail.
std::filesystem::path executable_directory() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    std::vector<char> buffer(4'096, 0);
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) return fs::path(std::string(buffer.data(), length)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size, 0);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        fs::path path = fs::weakly_canonical(fs::path(buffer.data()), ec);
        if (!ec) return path.parent_path();
        return fs::path(buffer.data()).parent_path();
    }
#else
    std::vector<char> buffer(4'096, 0);
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0) return fs::path(std::string(buffer.data(), static_cast<size_t>(length))).parent_path();
#endif
    return {};
}

}   // namespace

int main(int argc, char **argv) {
    // Strip the global --error-format option first so every subcommand sees a
    // clean argument vector, and so failures during the scan itself are
    // reported through the selected format.
    std::vector<std::string> filtered;
    filtered.emplace_back(argc > 0 ? argv[0] : "amc");
    bool explicit_error_format = false;
    bool format_json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--error-format") {
            if (i + 1 >= argc) {
                skl::amc::cli::fail(skl::amc::ErrorCategory::USAGE, "options", "--error-format requires a value");
                return 2;
            }
            explicit_error_format = true;
            if (!skl::amc::parse_error_format(argv[++i], skl::amc::cli::g_error_format)) {
                skl::amc::cli::fail(skl::amc::ErrorCategory::USAGE, "options", "--error-format must be text or json");
                return 2;
            }
            continue;
        }
        const std::string error_prefix = "--error-format=";
        if (argument.rfind(error_prefix, 0) == 0) {
            explicit_error_format = true;
            if (!skl::amc::parse_error_format(argument.substr(error_prefix.size()), skl::amc::cli::g_error_format)) {
                skl::amc::cli::fail(skl::amc::ErrorCategory::USAGE, "options", "--error-format must be text or json");
                return 2;
            }
            continue;
        }
        if (argument == "--format" && i + 1 < argc && std::string(argv[i + 1]) == "json") format_json = true;
        if (argument == "--format=json") format_json = true;
        filtered.push_back(argument);
    }
    if (!explicit_error_format && format_json) skl::amc::cli::g_error_format = skl::amc::ErrorFormat::JSON;

    std::vector<char *> argv_storage;
    argv_storage.reserve(filtered.size());
    for (auto &argument : filtered)
        argv_storage.push_back(argument.data());
    const int arg_count = static_cast<int>(filtered.size());
    char **args = argv_storage.data();

    if (arg_count < 2) {
        skl::amc::cli::print_usage();
        return 2;
    }
    const std::string command = args[1];
    fs::path executable_dir = executable_directory();
    if (executable_dir.empty()) executable_dir = fs::absolute(fs::path(args[0])).parent_path();
    const fs::path provider = executable_dir / "amc-cpp";

    if (command == "--list-languages") {
        skl::amc::compilation::ProviderManifest manifest;
        std::string manifest_error;
        if (!skl::amc::compilation::load_provider_manifest(executable_dir, manifest, manifest_error))
            return skl::amc::cli::fail(skl::amc::ErrorCategory::CONFIG, "provider_manifest", manifest_error);
        for (const auto &entry : manifest.providers)
            fmt::print("{}\n", entry.language);
        return 0;
    }
    if (command == "--describe-language") {
        if (arg_count != 3) return skl::amc::cli::usage_error("--describe-language requires a language");
        skl::amc::compilation::ProviderManifest manifest;
        std::string manifest_error;
        if (!skl::amc::compilation::load_provider_manifest(executable_dir, manifest, manifest_error))
            return skl::amc::cli::fail(skl::amc::ErrorCategory::CONFIG, "provider_manifest", manifest_error);
        const std::string language = args[2];
        if (const auto *entry = skl::amc::compilation::find_provider_manifest(manifest, language)) {
            fmt::print("{}\n", skl::amc::compilation::describe_provider(*entry));
            return 0;
        }
        std::string supported;
        for (const auto &entry : manifest.providers) {
            if (!supported.empty()) supported += ", ";
            supported += entry.language;
        }
        return skl::amc::cli::usage_error("--describe-language expects one of: " + supported);
    }

    if (command == "context") return skl::amc::cli::cmd_context(args, arg_count);
    if (command == "metadata") return skl::amc::cli::cmd_metadata(args, arg_count);
    if (command == "adapter") return skl::amc::cli::cmd_adapter(args, arg_count);
    if (command == "query") return skl::amc::cli::cmd_query(args, arg_count);
    if (command == "publish" || command == "fetch") return skl::amc::cli::cmd_symbols(args, arg_count);
    if (command == "verify") return skl::amc::cli::cmd_verify(args, arg_count, provider);
    if (command == "target") return skl::amc::cli::cmd_target(args, arg_count);
    if (command == "diff" || command == "compatibility") return skl::amc::cli::cmd_diff(args, arg_count);
    if (command == "cache") return skl::amc::cli::cmd_cache(args, arg_count);
    if (command == "generate" || command == "backend") return skl::amc::cli::cmd_generate(args, arg_count, provider);
    if (command == "frontend") return skl::amc::cli::cmd_frontend(args, arg_count, provider);
    if (command == "build") return skl::amc::compilation::build_command(args, arg_count, provider);
    if (command == "inspect" || command == "validate") return skl::amc::cli::cmd_inspect(args, arg_count);

    skl::amc::cli::print_usage();
    return 2;
}
