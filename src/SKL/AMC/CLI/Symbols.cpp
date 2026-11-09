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
#include "SKL/AMC/Platform/Symbol.h"
#include "SKL/AMC/Util/Error.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <filesystem>

#include <toml++/toml.h>
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

llvm::cl::SubCommand publish_subcommand("publish", "Publish an artifact to the symbol store");
llvm::cl::SubCommand fetch_subcommand("fetch", "Fetch an artifact from the symbol store");
llvm::cl::SubCommandGroup symbol_group{&publish_subcommand, &fetch_subcommand};
llvm::cl::opt<std::string> symbol_input(llvm::cl::Positional, llvm::cl::desc("[file.abix|binary]"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(symbol_group));
llvm::cl::opt<std::string> symbol_root("root", llvm::cl::desc("Symbol store root"), llvm::cl::value_desc("dir"),
    llvm::cl::cat(support::cli_category()), llvm::cl::sub(symbol_group));
llvm::cl::opt<std::string> symbol_output("o", llvm::cl::desc("Write the fetched artifact to a file"),
    llvm::cl::value_desc("file"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(symbol_group));
llvm::cl::opt<std::string> symbol_build_id("build-id", llvm::cl::desc("Build id or metadata key"),
    llvm::cl::value_desc("hex"), llvm::cl::cat(support::cli_category()), llvm::cl::sub(symbol_group));

}   // namespace

int cmd_symbols(bool publish) {
    {
        const fs::path input = symbol_input.getValue();
        const fs::path output = symbol_output.getValue();
        const fs::path root =
            symbol_root.empty() ? fs::path(amc::default_symbol_store_root()) : fs::path(symbol_root.getValue());
        const std::string &explicit_build_id = symbol_build_id;

        if (publish) {
            if (input.empty()) return usage_error("publish requires an input .abix or binary");
            std::ifstream file(input, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::IO, "publish", "cannot open input", input.string());
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            std::string error, key, meta, suffix;
            std::vector<uint8_t> artifact;
            if (amc::is_binary(bytes.data(), bytes.size())) {
                std::vector<uint8_t> build_id;
                if (!amc::read_build_id(bytes.data(), bytes.size(), build_id, error))
                    return fail(amc::ErrorCategory::FORMAT, "publish", error, input.string());
                key = amc::hex_encode(build_id.data(), build_id.size());
                if (!amc::find_binary_section(bytes.data(), bytes.size(), ".abix.metadata", artifact, error))
                    return fail(amc::ErrorCategory::FORMAT, "publish", error, input.string());
                amc::MetadataHeader header;
                if (!amc::read_metadata_header(artifact, header, error))
                    return fail(amc::ErrorCategory::FORMAT, "publish", error, input.string());
                meta = elf_meta_document(key, header);
                suffix = ".abixmeta";
            } else {
                amc::ABIModule module;
                if (!amc::load_module_source(input.string(), module, error))
                    return fail(read_error_category(error), "read_abix", error, input.string());
                amc::MetadataOptions options;
                std::vector<uint8_t> region;
                if (!amc::build_metadata_region(module, options, region, error))
                    return fail(amc::ErrorCategory::INTERNAL, "build_metadata_region", error, input.string());
                amc::MetadataHeader header;
                if (!amc::read_metadata_header(region, header, error))
                    return fail(amc::ErrorCategory::INTERNAL, "read_metadata_header", error, input.string());
                if (!explicit_build_id.empty()) {
                    if (!amc::normalize_build_id(explicit_build_id, key))
                        return usage_error("--build-id expects hexadecimal digits");
                } else if (fs::exists(input.string() + ".meta")) {
                    try {
                        const auto table = toml::parse_file(input.string() + ".meta");
                        const auto side = table["metadata"]["build_id"].value<std::string>();
                        std::string normalized;
                        if (side
                            && amc::normalize_build_id(*side, normalized)
                            && normalized.find_first_not_of('0') != std::string::npos)
                            key = normalized;
                    } catch (const toml::parse_error &) {
                        // A malformed sidecar is not fatal: fall back to MetadataID.
                    }
                }
                if (key.empty()) key = hash_key(header.metadata_id);
                artifact = bytes;
                meta = amc::metadata_meta_document(module, header);
                suffix = ".abix";
            }
            std::string stored_path;
            if (!amc::publish_symbol(root.string(), key, artifact, meta, suffix.c_str(), stored_path, error))
                return fail(amc::ErrorCategory::IO, "publish", error, input.string());
            fmt::print("published key={} path={}\n", key, stored_path);
            return 0;
        }

        std::string key;
        if (!explicit_build_id.empty()) {
            if (!amc::normalize_build_id(explicit_build_id, key))
                return usage_error("--build-id expects hexadecimal digits");
        } else {
            if (input.empty()) return usage_error("fetch requires a binary or --build-id");
            std::ifstream file(input, std::ios::binary);
            if (!file) return fail(amc::ErrorCategory::IO, "fetch", "cannot open input", input.string());
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            if (!amc::is_binary(bytes.data(), bytes.size()))
                return fail(amc::ErrorCategory::FORMAT, "fetch", "input is not an ELF or Mach-O binary", input.string(),
                    "pass --build-id to look up by id directly");
            std::vector<uint8_t> build_id;
            std::string error;
            if (!amc::read_build_id(bytes.data(), bytes.size(), build_id, error))
                return fail(amc::ErrorCategory::FORMAT, "fetch", error, input.string());
            key = amc::hex_encode(build_id.data(), build_id.size());
        }
        std::vector<uint8_t> artifact;
        std::string path, error;
        if (!amc::fetch_symbol(root.string(), key, artifact, path, error))
            return fail(amc::ErrorCategory::IO, "fetch", error);
        if (output.empty()) {
            fmt::print("{}\n", path);
            return 0;
        }
        try {
            auto out = fmt::output_file(output.string());
            out.print("{}", fmt::string_view(reinterpret_cast<const char *>(artifact.data()), artifact.size()));
        } catch (const std::exception &e) {
            return fail(amc::ErrorCategory::IO, "fetch", "cannot write output", output.string(), e.what());
        }
        fmt::print("fetched {} -> {}\n", path, output.string());
        return 0;
    }
}

}   // namespace skl::amc::cli
