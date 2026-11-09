#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace skl::amc::support {

// Canonical ABIX owner handle layout. Authored by
// SKL/ABIX/Interface/OwnerABI.h and exposed as ABIX_OWNER_HANDLE_JSON; AMC
// mirrors the 64-bit values and derives the 32-bit offsets from the target
// pointer width instead of hard-coding them at every call site.
struct OwnerHandleLayout {
    uint32_t pointer_size = 8;
    uint32_t size = 32;
    uint32_t align = 8;
    uint32_t object = 0;
    uint32_t mode = 8;
    uint32_t flags = 12;
    uint32_t destroy = 16;
    uint32_t module_instance = 24;
};

inline OwnerHandleLayout owner_handle_layout(uint32_t pointer_size) {
    OwnerHandleLayout layout;
    layout.pointer_size = pointer_size == 4 ? 4 : 8;
    if (layout.pointer_size == 8) {
        layout.size = 32;
        layout.align = 8;
        layout.object = 0;
        layout.mode = 8;
        layout.flags = 12;
        layout.destroy = 16;
        layout.module_instance = 24;
    } else {
        layout.size = 24;
        layout.align = 8;
        layout.object = 0;
        layout.mode = 4;
        layout.flags = 8;
        layout.destroy = 12;
        layout.module_instance = 16;
    }
    return layout;
}

inline bool is_owner_handle_name(std::string_view name) { return name.rfind("abix_owner_", 0) == 0; }

inline bool is_owned_string_name(std::string_view name) { return name.rfind("abix_owned_str", 0) == 0; }

inline bool is_owned_span_name(std::string_view name) { return name.rfind("abix_owned_span_", 0) == 0; }

}   // namespace skl::amc::support
