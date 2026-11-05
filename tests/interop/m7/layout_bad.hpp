#pragma once
#include <stdint.h>

// Deliberately different layout for the ABI mismatch compile-time test.
struct M7Layout {
    int32_t a;
    int32_t b;
};
