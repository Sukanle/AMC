# ABI metadata and shared declarations

<p align="center">
  <a href="abi-metadata_zh.md">中文</a> · English
</p>

## Status

| Item                                           | Status                            |
| ---------------------------------------------- | --------------------------------- |
| Owner handle layout (`ABIX_OWNER_HANDLE_JSON`) | available in v0.4.0               |
| Lease C ABI (`LeaseABI.h`)                     | available in v0.4.0               |
| Lease API metadata (`ABIX_LEASE_API_JSON`)     | available in the v0.4.x ABIX tree |
| Generated lease declarations (Rust/C++)        | available in AMC v0.4.0           |

## The duplicate-declaration problem

A cross-language ABI has one canonical definition but many consumers. Without shared metadata, each consumer keeps its own copy:

- C++ includes the ABIX header;
- Rust declares a `#[repr(C)]` mirror;
- Zig declares an `extern struct`;
- AMC hard-codes offsets or field sizes.

Every copy can drift. A field insertion, a changed `struct_size`, or a new status value becomes a runtime bug instead of a compile error.

## Single source of truth

ABIX owns the C ABI headers and publishes the layout as machine-readable metadata. AMC reads that metadata and generates the language declarations from it.

```text
ABIX OwnerABI.h / LeaseABI.h          (authoritative C ABI)
        │
        ├── ABIX_OWNER_HANDLE_JSON    (v0.4.0)
        └── ABIX_LEASE_API_JSON       (v0.4.x planned)
                    │
                    ▼
             AMC configure/codegen
                    │
        ┌───────────┴───────────┐
        ▼                       ▼
  C++ declarations        Rust/Zig declarations
  (alias + static_assert) (generated repr(C)/extern struct)
```

The generated code contains compile-time checks. A layout drift fails the build:

```cpp
static_assert(sizeof(abix_owner_handle) == 32);
static_assert(offsetof(abix_owner_handle, module_instance) == 24);
```

```rust
const _: () = assert!(core::mem::size_of::<abix_lease_api>() == ...);
const _: () = assert!(core::mem::offset_of!(abix_lease_api, acquire) == ...);
```

## What the feature eliminates

- Hand-maintained owner-handle offsets in AMC; `OwnerLayout.h` now reads `ABIX_OWNER_HANDLE_JSON` at configure time.
- Hand-maintained `abix_lease_api` size/offset/status values in the Rust projection.
- Per-language copies of `struct_size`, field offsets and status values.
- Silent drift between the C header and generated bindings.

## What is implemented

1. ABIX publishes `cmake/lease_api.json`, installed as `ABIX_LEASE_API_JSON`.
2. The file records `abi_version`, `struct_size`, `align`, field offsets and the stable `abix_status` values.
3. AMC CMake reads the JSON and defines the lease layout/version facts for `amc-rust`.
4. The Rust projection uses those facts for its constants and compile-time size/offset assertions; C++ uses `LeaseABI.h` directly.
5. The generated `LeaseGuard` consumes the shared declaration.

The JSON is additive metadata. It does not change the lease ABI, `abi_version`, or the `struct_size` rules. Older consumers can keep using `LeaseABI.h`.

## Remaining v0.4.x work

- Publish the ABIX metadata commit and move `AMC_ABIX_REF` from `30fc9be` to that ref. Until then AMC reads the metadata from the local checkout and falls back to the v1 layout when it is absent.
- Optionally ship full ABIX-owned Rust/Zig bindings; the Rust struct spelling is still emitted by AMC, but its layout and status facts already come from the metadata.

## Why this matters for `LeaseGuard`

`LeaseGuard<OwnerOf*>` needs `abix_lease_api`, `abix_lease_registry`, `abix_lease` and `abix_status`. If every generated language declares them separately, the guard can compile against a stale layout. Shared metadata makes the C header, the AMC runtime and every generated guard agree on one declaration.
