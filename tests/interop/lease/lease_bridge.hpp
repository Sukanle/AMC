// C ABI surface used by the Rust/Zig lease consumers.
//
// The Host (this shared library) owns the registry and the module table. Rust
// and Zig receive the `abix_lease_api` table and call `acquire` / `release`
// directly; they never see `ModuleControl` or `ModuleLease` layouts.
#pragma once

#include "SKL/ABIX/Interface/LeaseABI.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LeaseBridge LeaseBridge;

// Stable Host objects. `lease_host_api` and `lease_host_registry` must outlive
// every bridge and every lease created through them.
const abix_lease_api *lease_host_api(void);
abix_lease_registry *lease_host_registry(void);

// Load the provider, activate its ModuleControl and insert it into the Host
// module table. This does not acquire a lease; the consumer must call
// `api->acquire` before creating the object.
LeaseBridge *lease_host_load(const char *module_path);
abix_module_instance lease_host_module(LeaseBridge *bridge);

// Provider object operations. `lease_host_create` does not acquire a lease;
// callers that fail creation must release the lease themselves.
void *lease_host_create(LeaseBridge *bridge);
int32_t lease_host_destroy(LeaseBridge *bridge, void *object);
int32_t lease_host_object_get(LeaseBridge *bridge, void *object);
int32_t lease_host_object_set(LeaseBridge *bridge, void *object, int32_t value);

// Diagnostic: live objects created by this provider module. The takeover
// fixture uses it to prove `destroy` actually ran before the lease was released.
int32_t lease_host_live(LeaseBridge *bridge);

// Module lifecycle. `lease_host_unregister` begins draining and returns
// `ABIX_ERROR_BUSY` while a lease is live. `lease_host_finish` unloads the
// library after a successful unregister.
int32_t lease_host_unregister(LeaseBridge *bridge);
int32_t lease_host_finish(LeaseBridge *bridge);

// Registry lifecycle. Returns the `abix_status` value as an integer.
int32_t lease_host_registry_destroy(void);

// Diagnostic: the current ModuleControl lease count.
uint32_t lease_host_leases(LeaseBridge *bridge);

#ifdef __cplusplus
}
#endif
