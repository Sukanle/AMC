#include "api.hpp"
#include "amc_generated.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" int32_t on_value(void *context, int32_t value) {
    *static_cast<int32_t *>(context) += value;
    return 0;
}

extern "C" int32_t get_adapter(const void *object) { return l3_session_get(object); }
extern "C" int32_t set_adapter(void *object, int32_t value) { return l3_session_set(object, value); }

int main() {
    L3Session *session = l3_session_create("demo");
    assert(session != nullptr);

    int32_t total = 0;
    assert(l3_session_subscribe(session, &on_value, &total) == 0);
    assert(l3_session_emit(session, 7) == 0);
    assert(total == 7);

    auto ok = amc_generated::l3_session_div_result(session, 10, 2);
    assert(ok && ok.value == 5);
    auto err = amc_generated::l3_session_div_result(session, 10, 0);
    assert(!err && err.error.value == 1);
    assert(std::string(amc_generated::M7Errors::name(err.error.value)) == "div_zero");

    ::skl::abix::poly::InterfaceMethod methods[] = {
        {amc_generated::L3SessionOpsTraits::signatures[0], ::skl::abix::poly::interface_entry(&get_adapter), 0, 0},
        {amc_generated::L3SessionOpsTraits::signatures[1], ::skl::abix::poly::interface_entry(&set_adapter), 1, 0}
    };
    amc_generated::L3SessionOps proxy;
    assert(proxy.bind_methods(
               amc_generated::L3SessionOpsTraits::id, amc_generated::L3SessionOpsTraits::version, methods, 2, session)
           == ::skl::abix::poly::InterfaceStatus::ok);
    assert(proxy.l3_session_set(9) == 0);
    assert(proxy.l3_session_get() == 9);

    l3_session_destroy(session);
    std::printf("m7 cpp: OK\n");
    return 0;
}
