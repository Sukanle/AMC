#pragma once

// Shared MICS-backed enum name/value lookup.
//
// Each module publishes its own `*\/Reflect.h` header with SKL_RFS_ENUM
// registrations; these helpers only walk the registered TypeInfo table.

#include "SKL/MICS/MICS.h"

#include <cstddef>
#include <string_view>

namespace skl::amc::reflect {

template<typename E>
const char *enum_name(E value, const char *fallback) {
    for (const auto &entry : SRefl::TypeInfo<E>::list)
        if (entry.value == value) return entry.name.data();
    return fallback;
}

template<typename E>
E enum_value(std::string_view name, E fallback) {
    for (const auto &entry : SRefl::TypeInfo<E>::list) {
        if (entry.name.size() != name.size()) continue;
        bool equal = true;
        for (size_t i = 0; i < name.size(); ++i)
            if (entry.name.data()[i] != name[i]) {
                equal = false;
                break;
            }
        if (equal) return entry.value;
    }
    return fallback;
}

}   // namespace skl::amc::reflect
