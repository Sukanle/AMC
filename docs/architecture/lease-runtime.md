# Lease runtime and generated guards

<p align="center">
  <a href="lease-runtime_zh.md">中文</a> · English
</p>

## Status

| Layer                | Location                                     | Status      |
| -------------------- | -------------------------------------------- | ----------- |
| ABIX lease C ABI     | `SKL/ABIX/Interface/LeaseABI.h`              | ABIX v0.4.0 |
| Host runtime         | `SKL/AMC/Support/LeaseRegistry.h`            | AMC v0.4.0  |
| Generated C++ guard  | `LeaseGuard<OwnerOf*>` in the C++ projection | AMC v0.4.0  |
| Generated Rust guard | `LeaseGuard<O>` in `amc_generated.rs`        | AMC v0.4.0  |

## Two layers, one lifecycle

The lease path has two layers that are deliberately separate:

```text
Host runtime (AMC)                         Consumer wrapper
┌─────────────────────────────┐            ┌──────────────────────────────┐
│ skl::amc::support::         │            │ LeaseGuard<OwnerOf*>         │
│ LeaseRegistry               │            │                              │
│                             │            │ acquire(api, registry, m)    │
│ registry_create/destroy     │abix_lease  │ create() -> owner            │
│ acquire/release             │_api        │ Drop: destroy -> release     │
│ module table / draining     │            │                              │
└─────────────────────────────┘            └──────────────────────────────┘
```

- `LeaseRegistry` is the Host: it owns the registry, the module table, the lease count and the `abix_lease_api` function table.
- `LeaseGuard` is the consumer policy: it holds the owner handle, the registry handle and the lease token, and enforces `acquire -> create -> destroy -> release`.

The guard does not implement a registry, and the runtime does not know any generated owner type. The only shared contract is the ABIX C ABI.

## Host runtime

```cpp
#include "SKL/AMC/Support/LeaseRegistry.h"

skl::amc::support::LeaseRegistry host;
abix_lease_api api = host.make_api();

abix_lease_registry *registry = nullptr;
if (host.create_registry(&registry) != ABIX_SUCCESS) return 1;

skl::abix::poly::ModuleControl control;
control.name = "demo";
if (host.register_module(control) != ABIX_SUCCESS) return 2;

// Tell the provider which module instance owns its handles.
amc_module_set_instance(control.id);
```

`unregister_module` starts draining and returns `ABIX_ERROR_BUSY` while a lease is live. The module is not unloaded until every guard has released its lease.

## Generated C++ guard

```cpp
using Guard = skl::amc::generated::LeaseGuard<skl::amc::generated::OwnerOfWidget>;

auto guard = Guard::acquire(&api, registry, control.id, [] {
    return skl::amc::generated::OwnerOfWidget(widget_make(7));
});
if (!guard) return 3;
```

The guard is move-only. Dropping it calls the owner's destroy entry and then `api.release(host_context, registry, lease)`.

`LeaseGuard::adopt` takes an already-acquired lease and an owner handle when the Host has performed `acquire` itself.

## Generated Rust guard

```rust
let guard = LeaseGuard::<OwnerOfWidget>::acquire(api, registry, module, || {
    OwnerOfWidget::from_raw(widget_make(7))
})
.expect("lease guard");
```

The generated guard:

- rejects a null API, an unsupported `abi_version`, a short `struct_size` and missing `acquire`/`release` entries;
- releases the lease if `create` returns `None`;
- destroys the owner before releasing the lease in `Drop`;
- is move-only and does not implement `Clone`.

## Failure and lifetime rules

- `acquire` must happen before `create`; there is no create/acquire window.
- If creation fails, release the lease immediately.
- `destroy` runs before `release`.
- `registry_destroy` returns `ABIX_ERROR_BUSY` while leases are live.
- `unregister_module` returns `ABIX_ERROR_BUSY` while leases are live.
- A duplicate release of the same token is a no-op.
- `api`, `registry` and `host_context` must outlive every guard created from them.

## Declarations and metadata

The C++ guard uses ABIX's `LeaseABI.h` directly. The Rust projection currently emits a `#[repr(C)] abix_lease_api` declaration with compile-time size/offset assertions; [ABI metadata and shared declarations](abi-metadata.md) describes the planned `ABIX_LEASE_API_JSON` path that removes this duplicate declaration.

## Tests

- `tests/interop/lease/lease_test.py`: Host registry lifecycle, draining, duplicate release, concurrency and C++/Rust/Zig consumers.
- `tests/providers/rust/box_adapt_test.py`: generated C++ `LeaseGuard` and Rust `LeaseGuard` against a real Rust provider (`acquire`, create failure, adopt, module-instance check, destroy then release, invalid API rejection).

## Boundaries

- `Support/LeaseRegistry` depends on ABIX `Lease.h` / `LeaseABI.h`; it does not depend on a frontend, backend or CLI command.
- Generated guards depend only on the ABIX C ABI, not on AMC's runtime type, so a consumer can supply another Host implementation.
