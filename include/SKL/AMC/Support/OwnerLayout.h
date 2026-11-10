#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace skl::amc::support {

// Canonical ABIX owner handle layout. The C ABI is authored by
// SKL/ABIX/Interface/OwnerABI.h and its 64-bit layout is published as
// ABIX_OWNER_HANDLE_JSON. CMake reads that metadata and defines the
// AMC_ABIX_OWNER_* macros for targets that compile this header; the defaults
// below keep standalone consumers working. AMC and ABIX support 64-bit
// platforms only, so a 32-bit artifact cannot match this layout and is
// rejected by the owner-handle validation instead of receiving a derived
// 32-bit table.
#ifndef AMC_ABIX_OWNER_SIZE
#  define AMC_ABIX_OWNER_SIZE 32
#endif
#ifndef AMC_ABIX_OWNER_ALIGN
#  define AMC_ABIX_OWNER_ALIGN 8
#endif
#ifndef AMC_ABIX_OWNER_OBJECT
#  define AMC_ABIX_OWNER_OBJECT 0
#endif
#ifndef AMC_ABIX_OWNER_MODE
#  define AMC_ABIX_OWNER_MODE 8
#endif
#ifndef AMC_ABIX_OWNER_FLAGS
#  define AMC_ABIX_OWNER_FLAGS 12
#endif
#ifndef AMC_ABIX_OWNER_DESTROY
#  define AMC_ABIX_OWNER_DESTROY 16
#endif
#ifndef AMC_ABIX_OWNER_MODULE_INSTANCE
#  define AMC_ABIX_OWNER_MODULE_INSTANCE 24
#endif

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
    layout.pointer_size = pointer_size;
    layout.size = AMC_ABIX_OWNER_SIZE;
    layout.align = AMC_ABIX_OWNER_ALIGN;
    layout.object = AMC_ABIX_OWNER_OBJECT;
    layout.mode = AMC_ABIX_OWNER_MODE;
    layout.flags = AMC_ABIX_OWNER_FLAGS;
    layout.destroy = AMC_ABIX_OWNER_DESTROY;
    layout.module_instance = AMC_ABIX_OWNER_MODULE_INSTANCE;
    return layout;
}

inline bool is_owner_handle_name(std::string_view name) { return name.rfind("abix_owner_", 0) == 0; }

inline bool is_owned_string_name(std::string_view name) { return name.rfind("abix_owned_str", 0) == 0; }

inline bool is_owned_span_name(std::string_view name) { return name.rfind("abix_owned_span_", 0) == 0; }

}   // namespace skl::amc::support
