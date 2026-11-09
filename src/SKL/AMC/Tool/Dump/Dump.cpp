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
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/TypeMapping.h"
#include "SKL/AMC/Tool/Dump.h"

#include "SKL/AMC/Util/Error.h"

#include <fmt/format.h>
#include <fmt/os.h>

#include <exception>
#include <string>
#include <vector>

namespace {
void print_usage() {
    fmt::print(stderr,
        "usage: amc-dump <file.abix> [--json <output.json>]\n"
        "       amc-dump diff <source.abix> <target.abix> [--json <output.json>]\n"
        "global options:\n"
        "  --error-format text|json   structured error envelope (default text)\n");
}
}   // namespace

int main(int argc, char **argv) {
    // using namespace skl::amc::dump;
    // using namespace skl::amc::util;

    std::vector<std::string> filtered;
    filtered.emplace_back(argc > 0 ? argv[0] : "amc-dump");
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--error-format") {
            if (i + 1 >= argc)
                return skl::amc::util::fail(
                    skl::amc::ErrorCategory::USAGE, "options", "--error-format requires a value");
            if (!skl::amc::parse_error_format(argv[++i], skl::amc::util::g_error_format))
                return skl::amc::util::fail(
                    skl::amc::ErrorCategory::USAGE, "options", "--error-format must be text or json");
            continue;
        }
        const std::string error_prefix = "--error-format=";
        if (argument.rfind(error_prefix, 0) == 0) {
            if (!skl::amc::parse_error_format(argument.substr(error_prefix.size()), skl::amc::util::g_error_format))
                return skl::amc::util::fail(
                    skl::amc::ErrorCategory::USAGE, "options", "--error-format must be text or json");
            continue;
        }
        filtered.push_back(argument);
    }
    std::vector<char *> argv_storage;
    for (auto &argument : filtered)
        argv_storage.push_back(argument.data());
    const int arg_count = static_cast<int>(filtered.size());
    char **args = argv_storage.data();

    const bool diff = arg_count >= 2 && std::string(args[1]) == "diff";
    const bool json = (diff && arg_count == 6 && std::string(args[4]) == "--json")
                   || (!diff && arg_count == 4 && std::string(args[2]) == "--json");
    if ((diff && arg_count != 4 && !json) || (!diff && arg_count != 2 && !json)) {
        print_usage();
        return 2;
    }

    if (diff) {
        skl::amc::ABIModule source, target, report;
        std::string error;
        if (!skl::amc::read_abix(args[2], source, error))
            return skl::amc::util::fail(skl::amc::util::read_error_category(error), "read_abix", error, args[2]);
        if (!skl::amc::read_abix(args[3], target, error))
            return skl::amc::util::fail(skl::amc::util::read_error_category(error), "read_abix", error, args[3]);
        if (!skl::amc::ir::build_type_mapping(source, target, report, error))
            return skl::amc::util::fail(skl::amc::ErrorCategory::COMPATIBILITY, "build_compatibility", error);
        std::string text;
        if (!json) {
            skl::amc::dump::write_diff_text(source, target, report, text);
            fmt::print("{}", text);
            return 0;
        }
        skl::amc::dump::write_diff_json(source, target, report, text);
        try {
            auto output = fmt::output_file(args[5]);
            output.print("{}", text);
        } catch (const std::exception &e) {
            return skl::amc::util::fail(
                skl::amc::ErrorCategory::IO, "amc-dump", "cannot write JSON output", args[5], e.what());
        }
        return 0;
    }

    skl::amc::ABIModule module;
    std::string error;
    if (!skl::amc::read_abix(args[1], module, error))
        return skl::amc::util::fail(skl::amc::util::read_error_category(error), "read_abix", error, args[1]);
    std::string text;
    if (!json) {
        skl::amc::dump::write_text(module, text);
        fmt::print("{}", text);
        return 0;
    }
    skl::amc::dump::write_json(module, text);
    try {
        auto output = fmt::output_file(args[3]);
        output.print("{}", text);
    } catch (const std::exception &e) {
        return skl::amc::util::fail(
            skl::amc::ErrorCategory::IO, "amc-dump", "cannot write JSON output", args[3], e.what());
    }
    return 0;
}
