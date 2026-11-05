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
#ifndef AMC_CLI_SUPPORT_H
#define AMC_CLI_SUPPORT_H

#include <string>

namespace amc {
struct AbiModule;
struct Hash128;
struct MetadataHeader;
struct VerifyResult;
}   // namespace amc

namespace amc::cli {

// Shared CLI formatting/diagnostic helpers. Kept out of CLI.h so the command
// surface does not drag the IR into the interface layer.
std::string json_quote(const std::string &value);
bool parse_hash128(const std::string &text, amc::Hash128 &value);
std::string hash_key(amc::Hash128 value);
std::string elf_meta_document(const std::string &key, const amc::MetadataHeader &header);
std::string abi_diagnostics(const amc::AbiModule &reference, const amc::VerifyResult &result);

}   // namespace amc::cli

#endif   // AMC_CLI_SUPPORT_H
