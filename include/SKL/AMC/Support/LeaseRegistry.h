// Host implementation of the ABIX cross-language lease C ABI.
//
// ABIX defines `abix_lease_api`, `abix_lease_registry` and the status values in
// `SKL/ABIX/Interface/LeaseABI.h`. The Host owns the registry and the module
// table; this class is AMC's public reference Host. It is infrastructure, not
// part of the compiler frontend/backend dependency graph.
//
// Rules implemented here:
//   * the registry is registry-scoped: the same numeric token in two registries
//     is unrelated;
//   * acquire reuses the existing `ModuleControl` lease count;
//   * acquire and unregister are serialized by the module table mutex, so a
//     successful acquire can never be followed by an unload;
//   * `registry_destroy` enters CLOSING, returns BUSY while leases are live,
//     and rejects new acquires after CLOSING;
//   * release of an unknown or already-released token is a no-op.
#ifndef AMC_SUPPORT_LEASE_REGISTRY_H
#define AMC_SUPPORT_LEASE_REGISTRY_H

#include "SKL/ABIX/Interface/Lease.h"
#include "SKL/ABIX/Interface/LeaseABI.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace skl::amc::support {

class LeaseRegistry {
public:
    LeaseRegistry() = default;
    ~LeaseRegistry();

    LeaseRegistry(const LeaseRegistry &) = delete;
    LeaseRegistry &operator=(const LeaseRegistry &) = delete;

    // Fill a C ABI function table that points back at this Host. The Host must
    // outlive every registry and every lease created through the table.
    [[nodiscard]] abix_lease_api make_api() noexcept;

    // Host-side module table. `register_module` assigns a fresh
    // `ModuleInstanceId` and activates the control; `unregister_module` begins
    // draining and returns BUSY while any lease is still live.
    abix_status register_module(skl::abix::poly::ModuleControl &control);
    abix_status unregister_module(abix_module_instance module);

    // Synchronous lookup used by the Host and by tests. The returned pointer is
    // valid until the module is unregistered.
    [[nodiscard]] skl::abix::poly::ModuleControl *find_module(abix_module_instance module) noexcept;

    // C ABI entry points. These are public so a Host can expose them directly
    // without going through `make_api`.
    abix_status create_registry(abix_lease_registry **out_registry);
    abix_status destroy_registry(abix_lease_registry *registry);
    abix_status acquire(abix_lease_registry *registry, abix_module_instance module, abix_lease *out_lease);
    void release(abix_lease_registry *registry, abix_lease lease) noexcept;

    // Opaque per-registry state. Public only so the .cpp can cast the C handle;
    // it is not part of the Host's usable surface.
    struct Registry;

private:
    static abix_status registry_create_entry(void *context, abix_lease_registry **out_registry);
    static abix_status registry_destroy_entry(void *context, abix_lease_registry *registry);
    static abix_status acquire_entry(
        void *context, abix_lease_registry *registry, abix_module_instance module, abix_lease *out_lease);
    static void release_entry(void *context, abix_lease_registry *registry, abix_lease lease) noexcept;

    std::mutex _module_mutex;
    std::unordered_map<abix_module_instance, skl::abix::poly::ModuleControl *> _modules;

    std::mutex _registry_mutex;
    std::vector<Registry *> _registries;
};

}   // namespace skl::amc::support

#endif   // AMC_SUPPORT_LEASE_REGISTRY_H
