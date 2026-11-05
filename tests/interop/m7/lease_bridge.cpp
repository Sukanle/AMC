// A tiny C ABI bridge over ABIX's C++ ModuleControl / ModuleLease.
//
// Rust and Zig cannot include ABIX's C++ lease types. This bridge is the
// loader-side adapter used by the M7 fixture: it loads the module, activates
// the lifecycle control, acquires a real lease, and hands the opaque token to
// the generated Rust/Zig InterfaceLease.
#include "lease_bridge.hpp"

#include "api.hpp"

#include "SKL/ABIX/DLL/Object.h"
#include "SKL/ABIX/Interface/Discovery.h"
#include "SKL/ABIX/Interface/Lease.h"

#include <new>

using namespace skl::abix;
using namespace skl::abix::poly;

struct M7Lease {
    dll::Object library;
    ModuleControl control;
    ModuleLease *token = nullptr;
    L3Session *object = nullptr;
};

extern "C" M7Lease *m7_lease_load(const char *module_path) {
    auto *lease = new (std::nothrow) M7Lease();
    if (lease == nullptr) return nullptr;
    if (!lease->library.load(module_path)) {
        delete lease;
        return nullptr;
    }
    lease->control.name = "m7";
    bind_module_platform(lease->control, lease->library);
    if (!activate_module(lease->control)) {
        delete lease;
        return nullptr;
    }
    auto acquired = acquire_lease(lease->control);
    if (!acquired.valid()) {
        delete lease;
        return nullptr;
    }
    auto create = reinterpret_cast<L3Session *(*)(const char *)>(lease->library.resolve("l3_session_create"));
    if (create == nullptr) {
        delete lease;
        return nullptr;
    }
    lease->object = create("leased");
    // Keep the lease alive inside the bridge until the consumer takes the token.
    lease->token = new (std::nothrow) ModuleLease(std::move(acquired));
    if (lease->token == nullptr) {
        delete lease;
        return nullptr;
    }
    return lease;
}

extern "C" void *m7_lease_object(M7Lease *lease) { return lease != nullptr ? lease->object : nullptr; }

extern "C" void *m7_lease_token(M7Lease *lease) {
    if (lease == nullptr) return nullptr;
    auto *token = lease->token;
    lease->token = nullptr;
    return token;
}

extern "C" void m7_lease_release(void *token) { delete static_cast<ModuleLease *>(token); }

extern "C" int32_t m7_lease_destroy(M7Lease *lease) {
    if (lease == nullptr) return 1;
    auto destroy = reinterpret_cast<int32_t (*)(L3Session *)>(lease->library.resolve("l3_session_destroy"));
    if (lease->object != nullptr && destroy != nullptr) {
        destroy(lease->object);
        lease->object = nullptr;
    }
    if (lease->token != nullptr) {
        delete lease->token;
        lease->token = nullptr;
    }
    if (!begin_unload(lease->control)) {
        delete lease;
        return 1;
    }
    if (!wait_for_idle(lease->control)) {
        // A live lease still pins the module.
        return 2;
    }
    mark_unloaded(lease->control);
    lease->library.unload();
    delete lease;
    return 0;
}
