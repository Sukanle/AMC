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
#ifndef AMC_COMPILATION_CONFIG_H
#define AMC_COMPILATION_CONFIG_H

#include "SKL/AMC/Core/Core.h"

#include <filesystem>
#include <string>
#include <vector>

#include <toml++/toml.h>

namespace skl::amc::compilation {

// One `[[export]]` entry: output artifact and the optional symbol subset.
struct ExportSpec {
    std::filesystem::path output;
    std::vector<std::string> symbols;
};

// A4: configuration parsing shared by build/verify/generate. It is the first
// piece of the Compilation module; call sites should use this API rather than
// the CLI umbrella header.
bool parse_exports(const toml::table &table, std::vector<ExportSpec> &exports, std::string &error);

// Apply the `[target]` table to the module and recompute every LayoutHash with
// the Canonical Layout Encoding v1. Call once the target facts are final.
void apply_target_and_layout(const toml::table &config, ABIModule &module);

}   // namespace skl::amc::compilation

#endif   // AMC_COMPILATION_CONFIG_H
