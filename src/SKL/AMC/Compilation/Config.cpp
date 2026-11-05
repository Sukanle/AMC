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
#include "SKL/AMC/Compilation/Config.h"

namespace amc::compilation {

bool parse_exports(const toml::table &table, std::vector<ExportSpec> &exports, std::string &error) {
    const auto *entries = table["export"].as_array();
    if (!entries || entries->empty()) {
        error = "configuration needs at least one [[export]]";
        return false;
    }
    for (const auto &entry : *entries) {
        const auto *spec = entry.as_table();
        const auto output = spec ? (*spec)["output"].value<std::string>() : std::nullopt;
        if (!output || output->empty()) {
            error = "[[export]] requires output";
            return false;
        }
        ExportSpec result{*output, {}};
        if (const auto *symbols = (*spec)["symbols"].as_array())
            for (const auto &symbol : *symbols)
                result.symbols.push_back(symbol.value_or(""));
        exports.push_back(std::move(result));
    }
    return true;
}

void apply_target_and_layout(const toml::table &config, AbiModule &module) {
    if (const auto *target = config["target"].as_table()) {
        if (auto value = (*target)["arch"].value<int64_t>()) module.arch = static_cast<uint32_t>(*value);
        if (auto value = (*target)["os"].value<int64_t>()) module.os = static_cast<uint32_t>(*value);
        if (auto value = (*target)["abi"].value<int64_t>()) module.target_abi = static_cast<uint32_t>(*value);
        if (auto value = (*target)["pointer_bits"].value<int64_t>())
            module.pointer_bits = static_cast<uint32_t>(*value);
        const std::string endianness = (*target)["endianness"].value_or(std::string{});
        if (!endianness.empty()) module.endianness = endianness == "little" ? 1u : (endianness == "big" ? 2u : 0u);
    }
    recompute_layout_hashes(module);
}

}   // namespace amc::compilation
