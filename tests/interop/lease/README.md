# AMC Host lease fixture

This directory is AMC's reference Host implementation of the ABIX cross-language lease C ABI (`SKL/ABIX/Interface/LeaseABI.h`).

ABIX owns the contract; the Host owns the registry, the module table and the function table. The fixture proves the contract end to end without adding a runtime dependency to the compiler.

## Files

| File                                    | Role                                                                                                                          |
| --------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `host_lease.hpp` / `host_lease.cpp`     | Host-owned `abix_lease_registry`, module table and `abix_lease_api` implementation                                            |
| `host_lease_test.cpp`                   | C++ contract test: lifecycle, duplicate release, draining, registry CLOSING/BUSY, concurrent release, acquire/unregister race |
| `lease_bridge.hpp` / `lease_bridge.cpp` | C ABI bridge that loads a provider and exposes the Host function table to Rust/Zig                                            |
| `provider.cpp`                          | Minimal provider module with `lease_object_create` / `destroy`                                                                |
| `rust_consumer.rs`                      | Rust owned-object wrapper (`acquire -> create -> destroy -> release`, non-`Clone`)                                            |
| `rust_takeover.rs`                      | Rust cdylib that adopts a Zig-created object + lease (`destroy -> release`)                                                   |
| `zig_consumer.zig`                      | Zig owned-object wrapper with `deinit()`; hands the first session to Rust                                                     |
| `lease_test.py`                         | Builds and runs the C++/Rust/Zig consumers against the same ABIX checkout                                                     |

## Invariants exercised

- release must happen in the creating module and after `destroy`;
- the registry is registry-scoped, so equal token values in two registries are unrelated;
- `registry_destroy` enters CLOSING, returns BUSY while leases are live, and rejects new acquires;
- `unregister_module` begins draining and returns BUSY while leases are live;
- a duplicate release decrements at most once, including under concurrency;
- acquire and unregister cannot both succeed for the same unload;
- two DSOs may share one Host registry;
- a Zig-created object can be handed to a Rust consumer, which destroys it and releases the lease before Zig performs the final unregister (item 5 of the ABIX test matrix).

## Not covered yet

- registry creation/destruction from Rust/Zig (by contract, only the Host does this; consumers receive the function table).

## Run

```sh
python3 tests/interop/lease/lease_test.py <amc_bin_dir> <source_root>
```

The script needs a C++ compiler and `rustc`; the Zig consumer is skipped when `zig` is not available.
