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

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Support/Json.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/IR/Verify.h"
#include "SKL/AMC/Diagnostics/Verify.h"

#include <cctype>
#include <string>

#include <fmt/format.h>

namespace skl::amc::cli {

std::string json_quote(const std::string &value) { return "\"" + amc::json_escape(value) + "\""; }

// Parses "0x<hi16><lo16>" or "<hi16><lo16>" into a 128-bit hash.  The text
// order matches the canonical hash_string() rendering used across AMC.
bool parse_hash128(const std::string &text, amc::Hash128 &value) {
    std::string hex = text;
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
    if (hex.size() != 32) return false;
    for (char c : hex)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    value.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
    value.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
    return true;
}

// Renders a 128-bit value as 32 lowercase hex digits (hi then lo), matching
// the symbol-server key format derived from ELF build ids.
std::string hash_key(amc::Hash128 value) { return fmt::format("{:016x}{:016x}", value.hi, value.lo); }

// Minimal `.abix.meta` for a region extracted from a binary: the package/abi
// hash are not carried by the manifest, so only identity + counts are emitted.
std::string elf_meta_document(const std::string &key, const amc::MetadataHeader &header) {
    std::string out;
    out += "[metadata]\n";
    out += "build_id = \"0X";
    out += key;
    out += "\"\n";
    out += "metadata_id = \"0X";
    out += hash_key(header.metadata_id);
    out += "\"\n";
    out += "abi_version = ";
    out += fmt::to_string(header.version_major);
    out += "\n";
    out += "hash_algorithm = ";
    out += fmt::to_string(header.hash_algorithm);
    out += "\n";
    out += "type_count = ";
    out += fmt::to_string(header.type_count);
    out += "\n";
    out += "field_count = ";
    out += fmt::to_string(header.field_count);
    out += "\n";
    out += "function_count = ";
    out += fmt::to_string(header.function_count);
    out += "\n";
    out += "parameter_count = ";
    out += fmt::to_string(header.parameter_count);
    out += "\n";
    out += "symbol_count = ";
    out += fmt::to_string(header.symbol_count);
    out += "\n";
    out += "source = \"elf\"\n";
    return out;
}

// Editor-consumable ABI diagnostics: one `file:line:column: error: ...` line
// per drift, using the contract artifact's Source Origin when available.
std::string abi_diagnostics(const amc::ABIModule &reference, const amc::VerifyResult &result) {
    std::string output;
    for (const auto &diagnostic : amc::diagnostics::from_verify_result(reference, result)) {
        if (!diagnostic.file.empty()) {
            output += diagnostic.file;
            if (diagnostic.line != 0) {
                output += ":";
                output += fmt::to_string(diagnostic.line);
                if (diagnostic.column != 0) {
                    output += ":";
                    output += fmt::to_string(diagnostic.column);
                }
            }
            output += ": ";
        }
        output += amc::diagnostics::severity_name(diagnostic.severity);
        output += ": ";
        output += diagnostic.message;
        output += "\n";
    }
    return output;
}

// Machine-readable form of the same ABI diagnostics (`amc.diagnostics/1`).
std::string abi_diagnostics_json(const amc::ABIModule &reference, const amc::VerifyResult &result) {
    return amc::diagnostics::diagnostics_to_json(amc::diagnostics::from_verify_result(reference, result));
}

}   // namespace skl::amc::cli
