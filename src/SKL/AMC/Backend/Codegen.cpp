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
#include "SKL/AMC/Backend/Codegen.h"

#include <cctype>

#include <fmt/format.h>

namespace amc::backend {

std::string hex_u64(uint64_t value) { return fmt::format("0x{:x}", value); }

std::string hex_u64_ull(uint64_t value) { return fmt::format("0x{:x}ULL", value); }

std::string sanitize_identifier(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':') {
            result += '_';
            ++i;
        } else if (std::isalnum(static_cast<unsigned char>(name[i])) || name[i] == '_') {
            result += name[i];
        } else {
            result += '_';
        }
    }
    return result;
}

std::string escape_string_literal(std::string_view value) {
    std::string result = "\"";
    for (const char c : value) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"':  result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    result += '"';
    return result;
}

}   // namespace amc::backend
