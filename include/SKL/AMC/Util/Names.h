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
#ifndef AMC_UTIL_NAMES_H
#define AMC_UTIL_NAMES_H

#include <stdint.h>
#include <string>

#include "SKL/AMC/Core/Core.h"

namespace skl::amc::util {

// Human-readable renderings of the numeric target/function enums, shared by
// `amc-dump` and `amc-mcp` so both agree on the spelling.
std::string arch_name(uint32_t value);
std::string os_name(uint32_t value);
std::string compiler_name(uint32_t value);
std::string target_calling_convention_name(uint32_t value);
std::string function_calling_convention_name(uint32_t value);

// Stable names for the packed primitive ABI descriptor and map operations.
const char *primitive_kind_name(uint32_t kind);
const char *float_format_name(uint32_t format);
const char *map_opcode_name(amc::MapOpcode opcode);

}   // namespace skl::amc::util

#endif   // AMC_UTIL_NAMES_H
