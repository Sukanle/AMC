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
#ifndef AMC_UTIL_HEX_H
#define AMC_UTIL_HEX_H

#include <stdint.h>
#include <string>

#include <fmt/format.h>

#include "SKL/AMC/Core/Core.h"

namespace amc::util {

// Canonical lowercase hex rendering of a 128-bit identity (hi then lo), with
// the project-wide "0X" prefix used for BuildID / TypeID display.
inline std::string hash_hex(amc::Hash128 value) { return fmt::format("0X{:016x}{:016x}", value.hi, value.lo); }

// A fixed-width lowercase hex rendering of one 64-bit value.
inline std::string u64_hex(uint64_t value) { return fmt::format("{:016x}", value); }

}   // namespace amc::util

#endif   // AMC_UTIL_HEX_H
