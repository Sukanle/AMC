#include "lease_bridge.hpp"

#include "host_lease.hpp"

#include "SKL/ABIX/DLL/Object.h"
#include "SKL/ABIX/Interface/Discovery.h"
#include "SKL/ABIX/Interface/Lease.h"

#include <mutex>
#include <new>

namespace {

using CreateFn = void *(*)(const char *);
using DestroyFn = int32_t (*)(void *);
using GetFn = int32_t (*)(void *);
using SetFn = int32_t (*)(void *, int32_t);
using LiveFn = int32_t (*)(void);

struct HostState {
    skl::amc::test::lease::HostLeaseRegistry modules;
    abix_lease_api api;
    std::mutex registry_mutex;
    abix_lease_registry *registry = nullptr;

    HostState()
        : api(modules.make_api()) {}
};

HostState &host_state() {
    static HostState state;
    return state;
}

}   // namespace

struct LeaseBridge {
    skl::abix::dll::Object library;
    skl::abix::poly::ModuleControl control;
    CreateFn create = nullptr;
    DestroyFn destroy = nullptr;
    GetFn get = nullptr;
    SetFn set = nullptr;
    LiveFn live = nullptr;
    void *object = nullptr;
};

extern "C" const abix_lease_api *lease_host_api(void) { return &host_state().api; }

extern "C" abix_lease_registry *lease_host_registry(void) {
    HostState &state = host_state();
    std::lock_guard<std::mutex> lock(state.registry_mutex);
    if (state.registry != nullptr) return state.registry;
    if (state.api.registry_create(state.api.host_context, &state.registry) != ABIX_SUCCESS) return nullptr;
    return state.registry;
}

extern "C" LeaseBridge *lease_host_load(const char *module_path) {
    if (module_path == nullptr) return nullptr;
    auto *bridge = new (std::nothrow) LeaseBridge();
    if (bridge == nullptr) return nullptr;
    if (!bridge->library.load(module_path)) {
        delete bridge;
        return nullptr;
    }
    bridge->control.name = "lease-fixture";
    skl::abix::poly::bind_module_platform(bridge->control, bridge->library);
    if (host_state().modules.register_module(bridge->control) != ABIX_SUCCESS) {
        bridge->library.unload();
        delete bridge;
        return nullptr;
    }
    bridge->create = reinterpret_cast<CreateFn>(bridge->library.resolve("lease_object_create"));
    bridge->destroy = reinterpret_cast<DestroyFn>(bridge->library.resolve("lease_object_destroy"));
    bridge->get = reinterpret_cast<GetFn>(bridge->library.resolve("lease_object_get"));
    bridge->set = reinterpret_cast<SetFn>(bridge->library.resolve("lease_object_set"));
    bridge->live = reinterpret_cast<LiveFn>(bridge->library.resolve("lease_object_live"));
    if (bridge->create == nullptr
        || bridge->destroy == nullptr
        || bridge->get == nullptr
        || bridge->set == nullptr
        || bridge->live == nullptr) {
        (void)host_state().modules.unregister_module(bridge->control.id);
        bridge->library.unload();
        delete bridge;
        return nullptr;
    }
    return bridge;
}

extern "C" abix_module_instance lease_host_module(LeaseBridge *bridge) {
    return bridge != nullptr ? bridge->control.id : skl::abix::poly::invalid_module_instance;
}

extern "C" void *lease_host_create(LeaseBridge *bridge) {
    if (bridge == nullptr || bridge->create == nullptr) return nullptr;
    if (bridge->object == nullptr) bridge->object = bridge->create("leased");
    return bridge->object;
}

extern "C" int32_t lease_host_destroy(LeaseBridge *bridge, void *object) {
    if (bridge == nullptr || object == nullptr) return 0;
    if (bridge->destroy == nullptr) return 1;
    const int32_t result = bridge->destroy(object);
    if (bridge->object == object) bridge->object = nullptr;
    return result;
}

extern "C" int32_t lease_host_object_get(LeaseBridge *bridge, void *object) {
    if (bridge == nullptr || bridge->get == nullptr) return -1;
    return bridge->get(object);
}

extern "C" int32_t lease_host_object_set(LeaseBridge *bridge, void *object, int32_t value) {
    if (bridge == nullptr || bridge->set == nullptr) return -1;
    return bridge->set(object, value);
}

extern "C" int32_t lease_host_live(LeaseBridge *bridge) {
    if (bridge == nullptr || bridge->live == nullptr) return -1;
    return bridge->live();
}

extern "C" int32_t lease_host_unregister(LeaseBridge *bridge) {
    if (bridge == nullptr) return ABIX_ERROR_INVALID;
    return static_cast<int32_t>(host_state().modules.unregister_module(bridge->control.id));
}

extern "C" int32_t lease_host_finish(LeaseBridge *bridge) {
    if (bridge == nullptr) return 1;
    if (host_state().modules.find_module(bridge->control.id) != nullptr) return 1;
    bridge->library.unload();
    delete bridge;
    return 0;
}

extern "C" int32_t lease_host_registry_destroy(void) {
    HostState &state = host_state();
    std::lock_guard<std::mutex> lock(state.registry_mutex);
    if (state.registry == nullptr) return ABIX_ERROR_INVALID;
    const abix_status status = state.api.registry_destroy(state.api.host_context, state.registry);
    if (status == ABIX_SUCCESS) state.registry = nullptr;
    return static_cast<int32_t>(status);
}

extern "C" uint32_t lease_host_leases(LeaseBridge *bridge) {
    return bridge != nullptr ? bridge->control.leases.load(std::memory_order_acquire) : 0;
}
