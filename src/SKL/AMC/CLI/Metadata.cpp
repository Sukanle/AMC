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
#include "SKL/AMC/Platform/ELF.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Private/Support/File.h"
#include "SKL/AMC/Util/Error.h"

#include <filesystem>
#include <string>
#include <utility>
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

llvm::cl::SubCommand metadata_subcommand("metadata", "Read, verify or extract a metadata region");
llvm::cl::opt<std::string> metadata_input(llvm::cl::Positional, llvm::cl::desc("<file.abix|binary>"),
    llvm::cl::Required, llvm::cl::cat(support::cli_category()), llvm::cl::sub(metadata_subcommand));
llvm::cl::opt<std::string> metadata_format("format", llvm::cl::desc("Output format"),
    llvm::cl::value_desc("bin|meta|json|text"), llvm::cl::init("bin"), llvm::cl::cat(support::cli_category()),
    llvm::cl::sub(metadata_subcommand), support::format_callback());
llvm::cl::opt<std::string> metadata_output("o", llvm::cl::desc("Write output to a file"), llvm::cl::value_desc("file"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(metadata_subcommand));
llvm::cl::opt<std::string> metadata_build_id("build-id", llvm::cl::desc("Override the build id"),
    llvm::cl::value_desc("hex"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(metadata_subcommand));
llvm::cl::opt<bool> metadata_verify("verify", llvm::cl::desc("Verify an existing region or binary"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(metadata_subcommand));
llvm::cl::opt<bool> metadata_from_elf("from-elf", llvm::cl::desc("Read the region from an ELF/Mach-O binary"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(metadata_subcommand));
llvm::cl::opt<bool> metadata_no_names("no-names", llvm::cl::desc("Omit names"), llvm::cl::cat(support::cli_category()),
    llvm::cl::sub(metadata_subcommand));
llvm::cl::opt<bool> metadata_no_hash_index("no-hash-index", llvm::cl::desc("Omit the hash index"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(metadata_subcommand));

void write_bytes(const fs::path &path, const std::vector<uint8_t> &bytes, const char *stage, int &rc) {
    if (auto err = amc::support::write_binary_file(path.string(), bytes))
        rc = fail(amc::ErrorCategory::IO, stage, "cannot write output", path.string(),
            amc::support::error_message(std::move(err)));
}
}   // namespace

int cmd_metadata() {
    {
        const fs::path input = metadata_input.getValue();
        const fs::path output = metadata_output.getValue();
        const std::string &format = metadata_format;
        const bool include_names = !metadata_no_names;
        const bool include_hash_index = !metadata_no_hash_index;
        const bool verify = metadata_verify;
        const bool from_elf_flag = metadata_from_elf;
        amc::Hash128 build_id{};
        if (!metadata_build_id.empty() && !parse_hash128(metadata_build_id, build_id))
            return usage_error("--build-id expects 32 hexadecimal digits");

        if (verify) {
            if (format != "text" && format != "json" && format != "bin")
                return usage_error("metadata --verify --format must be text or json");
            auto bytes_or = amc::support::read_binary_file(input.string());
            if (!bytes_or)
                return fail(amc::ErrorCategory::IO, "metadata", "cannot open input", input.string(),
                    bytes_or.getError().message());
            std::vector<uint8_t> bytes = std::move(*bytes_or);
            std::vector<uint8_t> region;
            std::string error;
            // A raw region starts with the manifest; an ELF/Mach-O binary
            // embeds it in the `.abix.metadata` section.
            if (from_elf_flag || amc::is_binary(bytes.data(), bytes.size())) {
                if (!amc::find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", region, error))
                    return fail(amc::ErrorCategory::FORMAT, "metadata", error, input.string());
            } else {
                region = std::move(bytes);
            }
            amc::MetadataHeader header;
            if (!amc::read_metadata_header(region, header, error) || !amc::verify_metadata_region(region, error))
                return fail(amc::ErrorCategory::VALIDATION, "verify_metadata", error, input.string());
            if (format == "json") {
                fmt::print("{}", amc::metadata_header_to_json(header));
            } else {
                fmt::print("metadata: valid metadata_id=0X{:x}{:x} types={} fields={} functions={} symbols={}\n",
                    header.metadata_id.hi, header.metadata_id.lo, header.type_count, header.field_count,
                    header.function_count, header.symbol_count);
            }
            return 0;
        }

        if (from_elf_flag) {
            if (format != "bin" && format != "json")
                return usage_error("metadata --from-elf supports --format bin or json");
            auto bytes_or = amc::support::read_binary_file(input.string());
            if (!bytes_or)
                return fail(amc::ErrorCategory::IO, "metadata", "cannot open input", input.string(),
                    bytes_or.getError().message());
            std::vector<uint8_t> bytes = std::move(*bytes_or);
            std::vector<uint8_t> region;
            std::string error;
            if (!amc::find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", region, error))
                return fail(amc::ErrorCategory::FORMAT, "metadata", error, input.string());
            amc::MetadataHeader header;
            if (!amc::read_metadata_header(region, header, error))
                return fail(amc::ErrorCategory::FORMAT, "read_metadata_header", error, input.string());
            if (!amc::verify_metadata_region(region, error))
                return fail(amc::ErrorCategory::VALIDATION, "verify_metadata", error, input.string());
            if (format == "json") {
                fmt::print("{}", amc::metadata_header_to_json(header));
                return 0;
            }
            if (output.empty()) {
                fmt::print("{}", fmt::string_view(reinterpret_cast<const char *>(region.data()), region.size()));
                return 0;
            }
            int rc = 0;
            write_bytes(output, region, "metadata", rc);
            return rc;
        }

        if (format != "bin" && format != "meta" && format != "json")
            return usage_error("metadata --format must be bin, meta or json");

        amc::ABIModule module;
        std::string error;
        if (!amc::load_module_source(input.string(), module, error))
            return fail(read_error_category(error), "read_abix", error, input.string());

        amc::MetadataOptions options;
        options.build_id = build_id;
        options.include_names = include_names;
        options.include_hash_index = include_hash_index;
        std::vector<uint8_t> region;
        if (!amc::build_metadata_region(module, options, region, error))
            return fail(amc::ErrorCategory::INTERNAL, "build_metadata_region", error, input.string());
        amc::MetadataHeader header;
        if (!amc::read_metadata_header(region, header, error))
            return fail(amc::ErrorCategory::INTERNAL, "read_metadata_header", error, input.string());

        if (format == "bin") {
            if (output.empty()) {
                fmt::print("{}", fmt::string_view(reinterpret_cast<const char *>(region.data()), region.size()));
                return 0;
            }
            int rc = 0;
            write_bytes(output, region, "metadata", rc);
            return rc;
        }

        const std::string rendered =
            format == "json" ? amc::metadata_header_to_json(header) : amc::metadata_meta_document(module, header);
        if (output.empty()) {
            fmt::print("{}", rendered);
            return 0;
        }
        if (auto err = amc::support::write_text_file(output.string(), rendered))
            return fail(amc::ErrorCategory::IO, "metadata", "cannot write output", output.string(),
                amc::support::error_message(std::move(err)));
        return 0;
    }
}

namespace {
const bool metadata_registered = amc::support::register_command_handler(
    amc::support::Command::METADATA, [](const fs::path &) { return cmd_metadata(); });
}   // namespace

}   // namespace skl::amc::cli
