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
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace skl::amc::backend {

// Shared, language-neutral code generation helpers. Keeping these in one place
// stops the C++, Rust and Zig backends from drifting on identifier and literal
// spelling.

// Lowercase 0x-prefixed hexadecimal, without a suffix.
std::string hex_u64(uint64_t value);

// Lowercase 0x-prefixed hexadecimal with a C++ `ULL` suffix.
std::string hex_u64_ull(uint64_t value);

// Turn an ABI spelling into a C-family identifier: `::` collapses to a single
// `_` and every other non-alphanumeric character becomes `_`. Case is kept.
std::string sanitize_identifier(std::string_view name);

// Quote a value as a C-family string literal. Backslash, double quote, newline,
// carriage return and tab are escaped; everything else is copied verbatim.
std::string escape_string_literal(std::string_view value);

}   // namespace skl::amc::backend
