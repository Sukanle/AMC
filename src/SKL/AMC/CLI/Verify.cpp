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
#include "SKL/AMC/Private/Support/File.h"
#include "SKL/AMC/Support/CommandLine.h"
#include "SKL/AMC/CLI/Support.h"
#include "SKL/AMC/Support/Process.h"
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Artifact/Reflect.h"
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

#include <llvm/Support/CommandLine.h>

#include <fmt/format.h>

namespace fs = std::filesystem;
using skl::amc::util::fail;
using skl::amc::util::g_error_format;
using skl::amc::util::read_error_category;
using skl::amc::util::usage_error;

namespace skl::amc::cli {
namespace {

llvm::cl::SubCommand verify_subcommand("verify", "Verify a contract against an implementation");
llvm::cl::list<std::string> verify_inputs(llvm::cl::Positional,
    llvm::cl::desc("[<contract.abix> <implementation.abix>]"), llvm::cl::ZeroOrMore,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(verify_subcommand));
llvm::cl::opt<std::string> verify_config("c", llvm::cl::desc("Build a frontend result from a configuration"),
    llvm::cl::value_desc("file.abic.toml"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(verify_subcommand));
llvm::cl::opt<std::string> verify_build_dir("B", llvm::cl::desc("Build directory"), llvm::cl::value_desc("dir"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(verify_subcommand));
llvm::cl::opt<std::string> verify_format("format", llvm::cl::desc("Output format"),
    llvm::cl::value_desc("text|json|diagnostics|diagnostics-json"), llvm::cl::init("text"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(verify_subcommand), support::format_callback());

}   // namespace

int cmd_verify(const fs::path &provider) {
    const fs::path config_path = verify_config.getValue();
    const fs::path build_dir = verify_build_dir.empty() ? fs::path{} : fs::absolute(verify_build_dir.getValue());
    const std::vector<std::string> positionals(verify_inputs.begin(), verify_inputs.end());
    const std::string &format = verify_format;
    {
        if (format != "text" && format != "json" && format != "diagnostics" && format != "diagnostics-json")
            return usage_error("verify --format must be text, json, diagnostics or diagnostics-json");
        const bool json_output = format == "json";
        const bool diagnostics = format == "diagnostics";
        const bool diagnostics_json = format == "diagnostics-json";

        if (!positionals.empty()) {
            // Two-file form: amc verify <contract.abix> <implementation.abix>
            if (positionals.size() != 2)
                return usage_error("verify expects <contract.abix> <implementation.abix> or -c <file>.abic.toml");
            amc::ABIModule contract, implementation;
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
                return fail(amc::ErrorCategory::INTERNAL, "verify", error);
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
                    amc::ErrorCategory::CONFIG, "verify", "MVP requires exactly one [[import]]", config_path.string());
            std::vector<amc::compilation::ExportSpec> exports;
            std::string error;
            if (!amc::compilation::parse_exports(config, exports, error))
                return fail(amc::ErrorCategory::CONFIG, "verify", error, config_path.string());
            const auto *import_table = (*imports)[0].as_table();
            const std::string language =
                import_table ? import_table->get("language")->value_or(std::string("cpp")) : "cpp";
            const fs::path frontend = amc::compilation::provider_executable(provider, language);

            const fs::path base = build_dir.empty() ? fs::absolute(config_path).parent_path() : build_dir;
            const std::string process_suffix = amc::support::unique_process_suffix();
            const fs::path temporary = base / ".amc" / ("verify-frontend-" + process_suffix + ".abix");
            if (auto err = amc::support::make_directories(temporary.parent_path().string()))
                return fail(amc::ErrorCategory::IO, "verify", "cannot create build directory", base.string(),
                    amc::support::error_message(std::move(err)));
            if (!amc::compilation::dispatch_provider(
                    frontend, amc::support::Capability::FRONTEND, config_path, temporary, error))
                return fail(amc::ErrorCategory::PROVIDER, "frontend", error, config_path.string());

            amc::ABIModule full;
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
                if (!amc::support::path_exists(reference.string())) {
                    llvm::consumeError(amc::support::remove_path(temporary.string()));
                    return fail(amc::ErrorCategory::IO, "verify", "reference contract artifact not found",
                        reference.string(), "build the contract before verifying");
                }
                amc::ABIModule projected;
                if (!amc::project_symbols(full, spec.symbols, projected, error))
                    return fail(amc::ErrorCategory::VALIDATION, "project_symbols", error, config_path.string());
                const fs::path fresh = base / ".amc" / fmt::format("verify-{}-{}.abix", process_suffix, index);
                if (!amc::write_abix(projected, fresh.string(), error))
                    return fail(amc::ErrorCategory::IO, "write_abix", error, fresh.string());

                amc::ABIModule contract;
                if (!amc::read_abix(reference.string(), contract, error))
                    return fail(read_error_category(error), "read_abix", error, reference.string());
                amc::VerifyResult result;
                amc::pass::Context context;
                context.strict = false;
                amc::diagnostics::DiagnosticList compatibility_diagnostics;
                if (!amc::pass::compare_modules(contract, projected, result, context, compatibility_diagnostics))
                    return fail(amc::ErrorCategory::INTERNAL, "verify", error);
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
                llvm::consumeError(amc::support::remove_path(fresh.string()));
            }
            llvm::consumeError(amc::support::remove_path(temporary.string()));

            if (diagnostics)
                fmt::print("{}", diagnostics_buffer);
            else if (diagnostics_json)
                fmt::print("{}\n", amc::diagnostics::diagnostics_to_json(diagnostic_list));
            else
                fmt::print("{}", json_output ? amc::verify_report_to_json(results, consistent)
                                             : amc::verify_report_to_text(results));
            return consistent ? 0 : 1;
        } catch (const toml::parse_error &error) {
            return fail(amc::ErrorCategory::CONFIG, "verify", std::string(error.description()), config_path.string());
        }
    }
}

namespace {

llvm::cl::SubCommand target_subcommand("target", "Inspect or verify target facts");
llvm::cl::opt<std::string> target_input(llvm::cl::Positional, llvm::cl::desc("<file.abix|binary>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(target_subcommand));
llvm::cl::opt<std::string> target_expect("expect", llvm::cl::desc("Expected target facts"),
    llvm::cl::value_desc("arch=..,os=.."), llvm::cl::cat(support::cli_category()), llvm::cl::sub(target_subcommand));
llvm::cl::opt<std::string> target_format("format", llvm::cl::desc("Output format"), llvm::cl::value_desc("text|json"),
    llvm::cl::init("text"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(target_subcommand),
    support::format_callback());

const char *target_status_name(amc::TargetVerificationStatus status) {
    return skl::amc::reflect::enum_name(status, "unverifiable");
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
int cmd_target() {
    const fs::path input = target_input.getValue();
    const std::string &expect = target_expect;
    const std::string &format = target_format;
    if (format != "text" && format != "json") return usage_error("target --format must be text or json");

    amc::ABIModule module;
    std::string error;
    if (!amc::load_module_source(input.string(), module, error))
        return fail(read_error_category(error), "read_abix", error, input.string());
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(module, options, region, error))
        return fail(amc::ErrorCategory::INTERNAL, "target", error, input.string());
    amc::TargetDescription target;
    if (!amc::read_target_description(region, target, error))
        return fail(amc::ErrorCategory::INTERNAL, "target", error, input.string());

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
        return fail(amc::ErrorCategory::INTERNAL, "target", error, input.string());
    const bool incompatible = report.platform == amc::TargetVerificationStatus::INCOMPATIBLE
                           || report.layout == amc::TargetVerificationStatus::INCOMPATIBLE
                           || report.calling_convention == amc::TargetVerificationStatus::INCOMPATIBLE;
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

namespace {
const bool verify_registered = amc::support::register_command_handler(amc::support::Command::VERIFY,
    [](const fs::path &executable_dir) { return cmd_verify(executable_dir / "amc-cpp"); });
const bool target_registered = amc::support::register_command_handler(
    amc::support::Command::TARGET, [](const fs::path &) { return cmd_target(); });
}   // namespace

}   // namespace skl::amc::cli
