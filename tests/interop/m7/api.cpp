#include "api.hpp"

#include <cstring>
#include <string>

struct L3Session {
    std::string name;
    int32_t value = 0;
    l3_callback callback = nullptr;
    void *context = nullptr;
};

extern "C" {

M7_API struct L3Session *l3_session_create(const char *name) {
    auto *session = new (std::nothrow) L3Session();
    if (session != nullptr) session->name = name != nullptr ? name : "";
    return session;
}

M7_API int32_t l3_session_destroy(struct L3Session *session) {
    delete session;
    return 0;
}

M7_API int32_t l3_session_subscribe(struct L3Session *session, l3_callback callback, void *context) {
    if (session == nullptr) return 1;
    session->callback = callback;
    session->context = context;
    return 0;
}

M7_API int32_t l3_session_emit(struct L3Session *session, int32_t value) {
    if (session == nullptr || session->callback == nullptr) return 1;
    return session->callback(session->context, value);
}

M7_API int32_t l3_session_div(struct L3Session *session, int32_t a, int32_t b, int32_t *out) {
    if (session == nullptr || out == nullptr) return 99;
    if (b == 0) return 1;
    *out = a / b;
    return 0;
}

M7_API int32_t l3_session_get(const void *session) {
    return session != nullptr ? static_cast<const L3Session *>(session)->value : 0;
}

M7_API int32_t l3_session_set(void *session, int32_t value) {
    if (session == nullptr) return 1;
    static_cast<L3Session *>(session)->value = value;
    return 0;
}

}   // extern "C"
