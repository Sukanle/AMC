#pragma once

#include <stdint.h>

#if defined(_WIN32)
#  define M7_API __declspec(dllexport)
#else
#  define M7_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct L3Session;

struct M7Layout {
    int32_t a;
    int64_t b;
};

// A caller-owned callback: the library never stores beyond the session.
typedef int32_t (*l3_callback)(void *context, int32_t value);

M7_API struct L3Session *l3_session_create(const char *name);
M7_API int32_t l3_session_destroy(struct L3Session *session);
M7_API int32_t l3_session_subscribe(struct L3Session *session, l3_callback callback, void *context);
M7_API int32_t l3_session_emit(struct L3Session *session, int32_t value);

// Error boundary: 0 on success, 1 for division by zero, 99 for an unknown status.
M7_API int32_t l3_session_div(struct L3Session *session, int32_t a, int32_t b, int32_t *out);

// Handle-shaped interface operations.
M7_API int32_t l3_session_get(const void *session);
M7_API int32_t l3_session_set(void *session, int32_t value);

#ifdef __cplusplus
}
#endif
