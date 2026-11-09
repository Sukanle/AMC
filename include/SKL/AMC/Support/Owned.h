#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace skl::amc::support {

// Stable helper names for `create = "auto"` / `destroy = "auto"` ownership
// policies. The Rust frontend emits these thunks and the build layer resolves
// the same names when it links the [[owned]] record.
inline std::string auto_owned_name(std::string_view type, std::string_view action) {
    std::string name = "__amc_auto_";
    name += action;
    name += '_';
    for (const char c : type) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            name += c;
        else
            name += '_';
    }
    return name;
}

inline std::string auto_create_name(std::string_view type) { return auto_owned_name(type, "create"); }

inline std::string auto_destroy_name(std::string_view type) { return auto_owned_name(type, "destroy"); }

}   // namespace skl::amc::support
