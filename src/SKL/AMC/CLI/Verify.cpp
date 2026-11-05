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
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/IR/Verify.h"
#include "SKL/AMC/Diagnostics/Verify.h"
#include "SKL/AMC/Pass/Compatibility.h"
#include "SKL/AMC/Util/Error.h"
#include "SKL/AMC/Compilation/Config.h"
#include "SKL/AMC/Compilation/Provider.h"

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace fs = std::filesystem;
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

namespace amc::cli {

int cmd_verify(char **args, int arg_count, const fs::path &provider) {
    const std::string command = args[1];
    if (command == "verify") {
        fs::path config_path;
        fs::path build_dir;
        std::vector<std::string> positionals;
        std::string format = "text";
        for (int i = 2; i < arg_count; ++i) {
            const std::string argument = args[i];
            if ((argument == "-c" || argument == "-B" || argument == "--format") && i + 1 < arg_count) {
                const std::string value = args[++i];
                if (argument == "-c")
                    config_path = value;
                else if (argument == "-B")
                    build_dir = fs::absolute(value);
                else
                    format = value;
            } else if (argument.rfind("--format=", 0) == 0) {
                format = argument.substr(std::string("--format=").size());
            } else {
                positionals.push_back(argument);
            }
        }
        if (format != "text" && format != "json" && format != "diagnostics" && format != "diagnostics-json")
            return usage_error("verify --format must be text, json, diagnostics or diagnostics-json");
        const bool json_output = format == "json";
        const bool diagnostics = format == "diagnostics";
        const bool diagnostics_json = format == "diagnostics-json";

        if (!positionals.empty()) {
            // Two-file form: amc verify <contract.abix> <implementation.abix>
            if (positionals.size() != 2)
                return usage_error("verify expects <contract.abix> <implementation.abix> or -c <file>.abic.toml");
            amc::AbiModule contract, implementation;
            std::string error;
            if (!amc::load_module_source(positionals[0], contract, error))
                return fail(read_error_category(error), "read_abix", error, positionals[0]);
            if (!amc::load_module_source(positionals[1], implementation, error))
                return fail(read_error_category(error), "read_abix", error, positionals[1]);
            amc::VerifyResult result;
            amc::pass::Context context;
            context.strict = false;
            amc::diagnostics::DiagnosticList compatibility_diagnostics;
            if (!amc::pass::compare_modules(contract, implementation, result, context, compatibility_diagnostics))
                return fail(amc::ErrorCategory::internal, "verify", error);
            result.contract_path = positionals[0];
            result.implementation_path = positionals[1];
            if (diagnostics)
                fmt::print("{}", abi_diagnostics(contract, result));
            else if (diagnostics_json)
                fmt::print("{}", abi_diagnostics_json(contract, result));
            else
                fmt::print("{}", json_output ? amc::verify_report_to_json({result}, result.consistent)
                                             : amc::verify_report_to_text({result}));
            if (diagnostics_json) fmt::print("\n");
            return result.consistent ? 0 : 1;
        }

        if (config_path.empty())
            return usage_error("verify expects <contract.abix> <implementation.abix> or -c <file>.abic.toml");

        try {
            const auto config = toml::parse_file(config_path.string());
            const auto *imports = config["import"].as_array();
            if (!imports || imports->size() != 1)
                return fail(
                    amc::ErrorCategory::config, "verify", "MVP requires exactly one [[import]]", config_path.string());
            std::vector<amc::compilation::ExportSpec> exports;
            std::string error;
            if (!amc::compilation::parse_exports(config, exports, error))
                return fail(amc::ErrorCategory::config, "verify", error, config_path.string());
            const auto *import_table = (*imports)[0].as_table();
            const std::string language =
                import_table ? import_table->get("language")->value_or(std::string("cpp")) : "cpp";
            const fs::path frontend = amc::compilation::provider_executable(provider, language);

            const fs::path base = build_dir.empty() ? fs::absolute(config_path).parent_path() : build_dir;
            const fs::path temporary = base / ".amc" / "verify-frontend.abix";
            std::error_code ec;
            fs::create_directories(temporary.parent_path(), ec);
            if (ec)
                return fail(
                    amc::ErrorCategory::io, "verify", "cannot create build directory", base.string(), ec.message());
            if (!amc::compilation::dispatch_provider(frontend, "frontend", config_path, temporary, error))
                return fail(amc::ErrorCategory::provider, "frontend", error, config_path.string());

            amc::AbiModule full;
            if (!amc::read_abix(temporary.string(), full, error))
                return fail(read_error_category(error), "read_abix", error, temporary.string());
            full.package_name = config["package"]["name"].value_or("cpp");
            full.package_version = config["package"]["version"].value_or("");
            amc::compilation::apply_target_and_layout(config, full);

            std::vector<amc::VerifyResult> results;
            std::string diagnostics_buffer;
            amc::diagnostics::DiagnosticList diagnostic_list;
            bool consistent = true;
            for (size_t index = 0; index < exports.size(); ++index) {
                const auto &spec = exports[index];
                const fs::path reference = spec.output.is_absolute() ? spec.output : base / spec.output;
                if (!fs::exists(reference)) {
                    fs::remove(temporary, ec);
                    return fail(amc::ErrorCategory::io, "verify", "reference contract artifact not found",
                        reference.string(), "build the contract before verifying");
                }
                amc::AbiModule projected;
                if (!amc::project_symbols(full, spec.symbols, projected, error))
                    return fail(amc::ErrorCategory::validation, "project_symbols", error, config_path.string());
                const fs::path fresh = base / ".amc" / fmt::format("verify-{}.abix", index);
                if (!amc::write_abix(projected, fresh.string(), error))
                    return fail(amc::ErrorCategory::io, "write_abix", error, fresh.string());

                amc::AbiModule contract;
                if (!amc::read_abix(reference.string(), contract, error))
                    return fail(read_error_category(error), "read_abix", error, reference.string());
                amc::VerifyResult result;
                amc::pass::Context context;
                context.strict = false;
                amc::diagnostics::DiagnosticList compatibility_diagnostics;
                if (!amc::pass::compare_modules(contract, projected, result, context, compatibility_diagnostics))
                    return fail(amc::ErrorCategory::internal, "verify", error);
                result.contract_path = reference.string();
                result.implementation_path = config_path.string() + " (regenerated)";
                consistent = consistent && result.consistent;
                if (diagnostics) diagnostics_buffer += abi_diagnostics(contract, result);
                if (diagnostics_json) {
                    auto converted = amc::diagnostics::from_verify_result(contract, result);
                    diagnostic_list.insert(diagnostic_list.end(), std::make_move_iterator(converted.begin()),
                        std::make_move_iterator(converted.end()));
                }
                results.push_back(std::move(result));
                fs::remove(fresh, ec);
            }
            fs::remove(temporary, ec);

            if (diagnostics)
                fmt::print("{}", diagnostics_buffer);
            else if (diagnostics_json)
                fmt::print("{}\n", amc::diagnostics::diagnostics_to_json(diagnostic_list));
            else
                fmt::print("{}", json_output ? amc::verify_report_to_json(results, consistent)
                                             : amc::verify_report_to_text(results));
            return consistent ? 0 : 1;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::config, "verify", std::string(error.description()), config_path.string());
        }
    }
    return usage_error(command + ": unsupported subcommand");
}

namespace {

const char *target_status_name(amc::TargetVerificationStatus status) {
    switch (status) {
        case amc::TargetVerificationStatus::compatible:   return "compatible";
        case amc::TargetVerificationStatus::incompatible: return "incompatible";
        default:                                          return "unverifiable";
    }
}

// Parse `key=value,key=value` into a TargetExpectation (0 = do not check).
amc::TargetExpectation parse_expectation(const std::string &spec) {
    amc::TargetExpectation expected;
    size_t start = 0;
    while (start <= spec.size()) {
        const size_t comma = spec.find(',', start);
        const std::string pair = spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        const size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            const std::string key = pair.substr(0, equals);
            uint32_t value = 0;
            try {
                value = static_cast<uint32_t>(std::stoul(pair.substr(equals + 1)));
            } catch (...) {
                value = 0;
            }
            if (key == "arch")
                expected.arch = value;
            else if (key == "os")
                expected.os = value;
            else if (key == "abi")
                expected.target_abi = value;
            else if (key == "pointer_bits")
                expected.pointer_bits = value;
            else if (key == "endianness")
                expected.endianness = value;
            else if (key == "calling_convention")
                expected.calling_convention = value;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return expected;
}

}   // namespace

// `amc target <file.abix|binary> [--expect arch=..,os=..] [--format text|json]`
//
// Prints the recorded target description, or verifies it against an expected
// target. Verification reports platform / layout / calling convention
// independently; an unknown fact is `unverifiable`, never `compatible`. Exit
// status is 1 when any checked dimension is incompatible.
int cmd_target(char **args, int arg_count) {
    fs::path input;
    std::string expect;
    std::string format = "text";
    for (int i = 2; i < arg_count; ++i) {
        const std::string argument = args[i];
        if ((argument == "--expect" || argument == "--format") && i + 1 < arg_count) {
            const std::string value = args[++i];
            if (argument == "--expect")
                expect = value;
            else
                format = value;
        } else if (argument.rfind("--expect=", 0) == 0) {
            expect = argument.substr(std::string("--expect=").size());
        } else if (argument.rfind("--format=", 0) == 0) {
            format = argument.substr(std::string("--format=").size());
        } else if (input.empty()) {
            input = argument;
        } else {
            return usage_error("target expects <file.abix|binary> [--expect arch=..,os=..] [--format text|json]");
        }
    }
    if (input.empty())
        return usage_error("target expects <file.abix|binary> [--expect arch=..,os=..] [--format text|json]");
    if (format != "text" && format != "json") return usage_error("target --format must be text or json");

    amc::AbiModule module;
    std::string error;
    if (!amc::load_module_source(input.string(), module, error))
        return fail(read_error_category(error), "read_abix", error, input.string());
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(module, options, region, error))
        return fail(amc::ErrorCategory::internal, "target", error, input.string());
    amc::TargetDescription target;
    if (!amc::read_target_description(region, target, error))
        return fail(amc::ErrorCategory::internal, "target", error, input.string());

    if (expect.empty()) {
        if (format == "json")
            fmt::print(
                "{{\"schema\":\"abix.target/1\",\"present\":{},\"arch\":{},\"os\":{},\"abi\":{},\"compiler\":{},"
                "\"calling_convention\":{},\"pointer_bits\":{},\"endianness\":{}}}\n",
                target.present, target.arch, target.os, target.target_abi, target.compiler, target.calling_convention,
                target.pointer_bits, target.endianness);
        else if (!target.present)
            fmt::print("target: (none recorded)\n");
        else
            fmt::print("target: arch={} os={} abi={} compiler={} cc={} pointer_bits={} endianness={}\n", target.arch,
                target.os, target.target_abi, target.compiler, target.calling_convention, target.pointer_bits,
                target.endianness);
        return 0;
    }

    amc::AbiVerificationReport report;
    if (!amc::verify_abi(region, parse_expectation(expect), report, error))
        return fail(amc::ErrorCategory::internal, "target", error, input.string());
    const bool incompatible = report.platform == amc::TargetVerificationStatus::incompatible
                           || report.layout == amc::TargetVerificationStatus::incompatible
                           || report.calling_convention == amc::TargetVerificationStatus::incompatible;
    if (format == "json") {
        std::string reasons;
        for (const auto &reason : report.reasons) {
            if (!reasons.empty()) reasons += ",";
            reasons += "\"" + reason + "\"";
        }
        fmt::print(
            "{{\"schema\":\"abix.target/1\",\"platform\":\"{}\",\"layout\":\"{}\",\"calling_convention\":\"{}\","
            "\"reasons\":[{}]}}\n",
            target_status_name(report.platform), target_status_name(report.layout),
            target_status_name(report.calling_convention), reasons);
    } else {
        fmt::print("platform: {}\nlayout: {}\ncalling_convention: {}\n", target_status_name(report.platform),
            target_status_name(report.layout), target_status_name(report.calling_convention));
        for (const auto &reason : report.reasons)
            fmt::print("  - {}\n", reason);
    }
    return incompatible ? 1 : 0;
}

}   // namespace amc::cli
