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
#include "SKL/AMC/Private/Backend/Cpp/CppNames.h"

#include "SKL/AMC/Backend/Codegen.h"

#include <cctype>

namespace amc::lang::backend::cpp {

std::string cpp_name(std::string s) { return amc::backend::sanitize_identifier(s); }

bool is_cpp_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool is_cpp_qualified_name(const std::string &s) {
    if (s.empty() || s == "void") return false;
    size_t i = 0;
    while (i < s.size()) {
        if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) return false;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
            ++i;
        if (i == s.size()) return true;
        if (s.compare(i, 2, "::") != 0) return false;
        i += 2;
        if (i == s.size()) return false;   // trailing `::`
    }
    return true;
}

std::string cpp_string(const std::string &value) { return amc::backend::escape_string_literal(value); }

std::string hex_u64(uint64_t value) { return amc::backend::hex_u64_ull(value); }

std::string cpp_type_of(const amc::AbiModule &m, amc::Hash128 id) {
    if (id.lo == 0 && id.hi == 0) return "void";
    for (const auto &type : m.types)
        if (type.id == id) return type.name == "_Bool" ? std::string("bool") : type.name;
    return "void";
}

bool type_is_pointer(const amc::AbiModule &m, amc::Hash128 id) {
    for (const auto &type : m.types)
        if (type.id == id) return type.kind == amc::TypeKind::pointer;
    return false;
}

bool interface_has_handle_methods(const amc::AbiModule &m, const amc::InterfaceRecord &record) {
    if (record.method_count == 0) return false;
    for (uint32_t i = 0; i < record.method_count; ++i) {
        const auto &method = m.interface_methods[record.method_begin + i];
        if (method.parameters.empty() || !type_is_pointer(m, method.parameters[0].type_id)) return false;
        // The generated proxy forwards the runtime's `void *` object handle to
        // the typed function pointer. A typed handle (e.g. `const DemoVec2 *`)
        // would not implicitly convert; such contracts remain metadata-only
        // until an explicit void-pointer adapter is exposed.
        const std::string first = cpp_type_of(m, method.parameters[0].type_id);
        if (first != "void*" && first != "const void*") return false;
    }
    return true;
}

}   // namespace amc::lang::backend::cpp
