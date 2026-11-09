#pragma once

#include "SKL/AMC/Support/EnumReflect.h"

#include <string_view>

namespace skl::amc::support {

// Source/projection languages and provider capabilities. The spellings are
// registered once here; provider constructors, CLI dispatch and the JSONL
// protocol all use these enum values instead of string comparisons.
enum class Language {
    UNKNOWN,
    CPP,
    LUA,
    RUST,
    ZIG,
};

enum class Capability {
    UNKNOWN,
    FRONTEND,
    BACKEND,
};

inline const char *language_name(Language language) { return amc::reflect::enum_name(language, "unknown"); }

inline Language language_from(std::string_view name) {
    return amc::reflect::enum_value<Language>(name, Language::UNKNOWN);
}

inline const char *capability_name(Capability capability) { return amc::reflect::enum_name(capability, "unknown"); }

inline Capability capability_from(std::string_view name) {
    return amc::reflect::enum_value<Capability>(name, Capability::UNKNOWN);
}

}   // namespace skl::amc::support

SKL_RFS_ENUM(skl::amc::support::Language)
SKL_RFS_ENUM_VALUE(skl::amc::support::Language::CPP, "cpp")
SKL_RFS_ENUM_VALUE(skl::amc::support::Language::LUA, "lua")
SKL_RFS_ENUM_VALUE(skl::amc::support::Language::RUST, "rust")
SKL_RFS_ENUM_VALUE(skl::amc::support::Language::ZIG, "zig")
SKL_RFS_ENUM()

SKL_RFS_ENUM(skl::amc::support::Capability)
SKL_RFS_ENUM_VALUE(skl::amc::support::Capability::FRONTEND, "frontend")
SKL_RFS_ENUM_VALUE(skl::amc::support::Capability::BACKEND, "backend")
SKL_RFS_ENUM()
