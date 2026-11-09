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
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Pass/TypeMapping.h"
#include "SKL/AMC/Util/Error.h"

#include <filesystem>
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

llvm::cl::SubCommand diff_subcommand("diff", "Compare two artifacts");
llvm::cl::SubCommand compatibility_subcommand("compatibility", "Compare two artifacts");
llvm::cl::SubCommandGroup diff_group{&diff_subcommand, &compatibility_subcommand};
llvm::cl::opt<std::string> diff_source(llvm::cl::Positional, llvm::cl::desc("<source.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(diff_group));
llvm::cl::opt<std::string> diff_target(llvm::cl::Positional, llvm::cl::desc("<target.abix>"), llvm::cl::Required,
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(diff_group));
llvm::cl::opt<std::string> diff_output("o", llvm::cl::desc("Write the compatibility report"),
    llvm::cl::value_desc("report.abix"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(diff_group));

}   // namespace

int cmd_diff(bool compatibility) {
    const fs::path source_path = diff_source.getValue();
    const fs::path target_path = diff_target.getValue();
    const fs::path output = diff_output.getValue();
    {
        if (compatibility && !output.empty()) return usage_error("compatibility does not accept -o; use diff");
        amc::ABIModule source, target, report;
        std::string error;
        if (!amc::load_module_source(source_path, source, error))
            return fail(read_error_category(error), "read_abix", error, source_path);
        if (!amc::load_module_source(target_path, target, error))
            return fail(read_error_category(error), "read_abix", error, target_path);
        amc::pass::Context context;
        context.strict = false;
        amc::diagnostics::DiagnosticList diagnostics;
        if (!amc::pass::build_compatibility_report(source, target, report, context, diagnostics)) {
            const std::string message = diagnostics.empty() ? error : diagnostics.front().message;
            return fail(amc::ErrorCategory::COMPATIBILITY, "build_compatibility", message);
        }
        bool incompatible = false;
        for (const auto &record : report.compatibility) {
            fmt::print("{} {}:{} -> {}:{}", amc::compatibility_name(record.kind), record.source_type.lo,
                record.source_type.hi, record.target_type.lo, record.target_type.hi);
            if (record.map_index != UINT32_MAX) fmt::print(" map={}", record.map_index);
            fmt::print("\n");
            incompatible = incompatible || record.kind == amc::CompatibilityKind::INCOMP;
        }
        if (!output.empty() && !amc::write_abix(report, output, error))
            return fail(amc::ErrorCategory::IO, "write_abix", error, output);
        return compatibility && incompatible ? 1 : 0;
    }
}

}   // namespace skl::amc::cli
