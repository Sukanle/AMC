#include "SKL/AMC/Support/LeaseRegistry.h"

#include <new>
#include <utility>

namespace skl::amc::support {

using skl::abix::poly::begin_unload;
using skl::abix::poly::mark_unloaded;
using skl::abix::poly::ModuleControl;
using skl::abix::poly::next_module_instance_id;
using skl::abix::poly::release_lease;
using skl::abix::poly::try_acquire_lease;

// The C ABI type is opaque. A registry handle is a pointer to this Host-owned
// state; callers must never interpret it as anything else.
struct LeaseRegistry::Registry {
    bool closing = false;
    bool destroyed = false;
    uint64_t next_token = 1;
    std::unordered_map<abix_lease, ModuleControl *> leases;
    std::mutex mutex;
};

namespace {

LeaseRegistry::Registry *state_of(abix_lease_registry *registry) noexcept {
    return reinterpret_cast<LeaseRegistry::Registry *>(registry);
}

}   // namespace

LeaseRegistry::~LeaseRegistry() {
    std::lock_guard<std::mutex> lock(_registry_mutex);
    for (Registry *registry : _registries)
        delete registry;
    _registries.clear();
}

abix_lease_api LeaseRegistry::make_api() noexcept {
    abix_lease_api api{};
    api.abi_version = ABIX_LEASE_API_VERSION;
    api.struct_size = sizeof(abix_lease_api);
    api.host_context = this;
    api.registry_create = &LeaseRegistry::registry_create_entry;
    api.registry_destroy = &LeaseRegistry::registry_destroy_entry;
    api.acquire = &LeaseRegistry::acquire_entry;
    api.release = &LeaseRegistry::release_entry;
    return api;
}

abix_status LeaseRegistry::register_module(ModuleControl &control) {
    std::lock_guard<std::mutex> lock(_module_mutex);
    if (control.id == skl::abix::poly::invalid_module_instance) control.id = next_module_instance_id();
    if (!skl::abix::poly::activate_module(control)) return ABIX_ERROR_INVALID;
    _modules[control.id] = &control;
    return ABIX_SUCCESS;
}

abix_status LeaseRegistry::unregister_module(abix_module_instance module) {
    std::lock_guard<std::mutex> lock(_module_mutex);
    auto it = _modules.find(module);
    if (it == _modules.end()) return ABIX_ERROR_NOT_FOUND;
    ModuleControl &control = *it->second;
    if (!begin_unload(control)) return ABIX_ERROR_INVALID;
    if (control.leases.load(std::memory_order_acquire) != 0) return ABIX_ERROR_BUSY;
    mark_unloaded(control);
    _modules.erase(it);
    return ABIX_SUCCESS;
}

ModuleControl *LeaseRegistry::find_module(abix_module_instance module) noexcept {
    std::lock_guard<std::mutex> lock(_module_mutex);
    auto it = _modules.find(module);
    return it == _modules.end() ? nullptr : it->second;
}

abix_status LeaseRegistry::create_registry(abix_lease_registry **out_registry) {
    if (out_registry == nullptr) return ABIX_ERROR_INVALID;
    *out_registry = nullptr;
    auto *registry = new (std::nothrow) Registry();
    if (registry == nullptr) return ABIX_ERROR_INVALID;
    {
        std::lock_guard<std::mutex> lock(_registry_mutex);
        _registries.push_back(registry);
    }
    *out_registry = reinterpret_cast<abix_lease_registry *>(registry);
    return ABIX_SUCCESS;
}

abix_status LeaseRegistry::destroy_registry(abix_lease_registry *registry) {
    if (registry == nullptr) return ABIX_ERROR_INVALID;
    Registry &state = *state_of(registry);
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.destroyed) return ABIX_ERROR_INVALID;
    state.closing = true;
    if (!state.leases.empty()) return ABIX_ERROR_BUSY;
    state.destroyed = true;
    return ABIX_SUCCESS;
}

abix_status LeaseRegistry::acquire(abix_lease_registry *registry, abix_module_instance module, abix_lease *out_lease) {
    if (registry == nullptr || out_lease == nullptr) return ABIX_ERROR_INVALID;
    *out_lease = ABIX_LEASE_INVALID;

    Registry &state = *state_of(registry);
    std::lock_guard<std::mutex> registry_lock(state.mutex);
    if (state.destroyed || state.closing) return ABIX_ERROR_UNLOADING;

    ModuleControl *control = nullptr;
    {
        // The module-table lock makes "check state + increment lease count" one
        // decision with respect to unregister_module.
        std::lock_guard<std::mutex> module_lock(_module_mutex);
        auto it = _modules.find(module);
        if (it == _modules.end()) return ABIX_ERROR_NOT_FOUND;
        control = it->second;
        if (!try_acquire_lease(*control)) return ABIX_ERROR_UNLOADING;
    }

    if (state.next_token == 0) {
        release_lease(*control);
        return ABIX_ERROR_INVALID;
    }
    const abix_lease token = state.next_token;
    state.next_token = token == UINT64_MAX ? 0 : token + 1;
    state.leases.emplace(token, control);
    *out_lease = token;
    return ABIX_SUCCESS;
}

void LeaseRegistry::release(abix_lease_registry *registry, abix_lease lease) noexcept {
    if (registry == nullptr || lease == ABIX_LEASE_INVALID) return;
    Registry &state = *state_of(registry);
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.leases.find(lease);
    if (it == state.leases.end()) return;
    ModuleControl *control = it->second;
    state.leases.erase(it);
    release_lease(*control);
}

abix_status LeaseRegistry::registry_create_entry(void *context, abix_lease_registry **out_registry) {
    if (context == nullptr) return ABIX_ERROR_INVALID;
    return static_cast<LeaseRegistry *>(context)->create_registry(out_registry);
}

abix_status LeaseRegistry::registry_destroy_entry(void *context, abix_lease_registry *registry) {
    if (context == nullptr) return ABIX_ERROR_INVALID;
    return static_cast<LeaseRegistry *>(context)->destroy_registry(registry);
}

abix_status LeaseRegistry::acquire_entry(
    void *context, abix_lease_registry *registry, abix_module_instance module, abix_lease *out_lease) {
    if (context == nullptr) return ABIX_ERROR_INVALID;
    return static_cast<LeaseRegistry *>(context)->acquire(registry, module, out_lease);
}

void LeaseRegistry::release_entry(void *context, abix_lease_registry *registry, abix_lease lease) noexcept {
    if (context == nullptr) return;
    static_cast<LeaseRegistry *>(context)->release(registry, lease);
}

}   // namespace skl::amc::support
