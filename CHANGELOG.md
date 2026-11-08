# Changelog

## v0.3.3 — 2026-11-09

### Added

- AMC Host implementation of the ABIX cross-language lease C ABI: `abix_lease_registry`, the `abix_lease_api` function table, synchronous module-instance lookup, CLOSING/BUSY states and draining validation.
- `tests/interop/lease/`: a C++ contract test plus Rust/Zig owned-object wrappers that exercise acquire -> create -> destroy -> release, draining, duplicate release and registry destroy.
- Zig -> Rust handoff fixture: Zig creates the object and lease, a Rust cdylib adopts them (`destroy -> release`) and Zig performs the final unregister.
- `amc-lease-interop-test`: builds the provider, Host bridge and C++/Rust/Zig consumers against the same ABIX checkout.
- Explicit `[[rust.expose]]` handoff: AMC records the exported symbol and emits `#[export_name = "<symbol>"] pub extern "C"` wrappers for ordinary Rust functions. `amc-rust-expose-test` compiles and calls the generated wrappers.
- Rust ownership / error contract handoff: `[[owned]]`, `[[contract]]` and `[[error]]` are validated end to end for Rust; `amc-rust-contract-test` compiles a native provider and checks the generated RAII and `Result` wrappers.

### Changed

- Track ABIX v0.3.4 (`AMC_ABIX_REF=v0.3.4`), the docs-only lockstep correction on top of the additive v0.3.3 surface; no STD layout changes.
- `[export] symbols` no longer records a plain Rust function as C ABI; use `[[rust.expose]]` to generate an explicit wrapper.
- Fixed duplicate TypeIDs when a Rust pointer or array spelling appears in more than one signature.
- Rust projections preserve `isize` / `usize` and map `c_long` / `c_ulong` to `core::ffi::c_long` / `c_ulong` instead of collapsing every pointer-sized primitive to `c_longlong`.
- Windows x64 (`os=1`) now defaults `c_long` / `c_ulong` to 32 bits; an explicit `c_long_bits` still overrides it.

### Fixed

- `amc` invoked through PATH (a bare `argv[0]`) now resolves `amc-<language>` next to the real executable instead of the current directory. Installed tools no longer fail with "provider closed the response stream".

### Compatibility

- ABIX v0.4.0's breaking `abix_optional` / `abix_result` / `abix_str` update is not part of this release. AMC v0.4.0 will perform the lockstep migration and regenerate all artifacts.

### Known limitations

- cross-target probe remains future work; the Host layout probe is stable.
- `packed` / `transparent` / `union` backend emission is deferred to v0.4.0, together with the first-class representation model. v0.3.x keeps layout validation only.
- Windows and macOS validation is the final pre-tag gate for this release.

### Validation

- Linux: CTest 34/34 passed.
- `amc-boundary-contracts` and `amc-boundary-edges --strict`: passed.
- `amc-lease-interop-test`, `amc-rust-expose-test`, `amc-rust-contract-test`, `amc-rust-clong-test`: passed.
- `amc-provider-path-test`: passed.
- Install smoke: `cmake --install` followed by `amc build` invoked through PATH.
- Windows/macOS: to be validated before the v0.3.3 tag is created.

## v0.3.2 — 2026-11-07

### Added

- Frozen `amc.rust.semantic/1` adapter contract for the future rustc helper, including target, crate, types, fields, functions and parameter ownership.
- `amc-rust-semantic-schema-test`: validates the sample document, unknown optional fields and required-field failures.
- Rust layout support for `#[repr(transparent)]`, `#[repr(C, packed(N))]` and `#[repr(C)]` unions.
- rustc probe coverage for transparent, packed(N) and union layout.

### Changed

- v0.3.x Rust work is now explicitly a transition adapter for the v0.4.0 rustc frontend; full trait/generic/macro/Cargo semantics are deferred.
- Rust parser validates `repr(transparent)` as a single-field layout.

### Known limitations

- advanced repr forms are layout-validated only; C++/Rust/Zig backend emission for packed and union is not complete.
- `repr(transparent)` does not yet model extra zero-sized fields.
- cross-target probe, explicit `[[rust.expose]]` wrapper handoff and error/ownership contract handoff remain later v0.3.x work.

### Validation

- CTest: 29/29 passed.
- `boundary --strict`: passed.
- `amc-rust-layout-probe-test`: passed.
- `amc-rust-semantic-schema-test`: passed.

## v0.3.1 — 2026-11-07

### Added

- Rust FFI surface now reads `[target]` facts (`arch`, `os`, `abi`, `pointer_bits`, `endianness`, optional `c_long_bits`) and records them in the ABI module.
- `amc-rust-layout-probe-test`: compares AMC layout with a generated rustc `size_of` / `align_of` / `offset_of!` probe.
- Rust parity fixtures for pointer-sized primitives, fixed arrays, type aliases, non-default enum representation and packed structs.
- `repr(C, packed)` layout validation against rustc.
- Void-returning `extern "C"` functions are now recorded instead of skipped.
- Host target validation against `rustc --print cfg`.

### Changed

- **Breaking for Rust imports:** a Rust `.abic.toml` now requires a `[target]` section with `pointer_bits`. Existing configs must add:

  ```toml
  [target]
  arch = 1
  os = 1
  abi = 1
  pointer_bits = 64
  endianness = "little"
  ```

- Rust `pointer`, `isize`, `usize`, `c_long` and `c_ulong` are target-aware; the old hard-coded 8-byte assumption is removed.
- Project and provider versions are aligned to `0.3.1`; the MCP server version is aligned as well.

### Fixed

- `#[repr(C, packed)]` field offsets are now packed; previously only the overall alignment was reduced.
- `#[repr(C, packed)]` layout is validated against rustc.
- Rust functions returning `()` are no longer silently dropped.

### Known limitations

- The layout probe is Host-only. Cross-target reflection is deferred.
- `repr(C, packed)` is layout-validated but backend emission for packed types is not implemented yet; it is not advertised as a complete projection feature.
- Primitive aliases such as `isize` / `c_long` may share the ABI type with `i32` / `i64`; source-spelling retention is a follow-up.
- Windows x64 users must set `c_long_bits = 32` explicitly when the target uses the Windows C ABI.
- Advanced Rust semantics (trait, associated type, trait object, complex lifetimes, Cargo workspace) remain v0.4.0 work.

### Validation

- CTest: 28/28 passed.
- `boundary --strict`: passed.
- `amc-rust-test`: passed.
- `amc-rust-layout-probe-test`: passed.

## v0.3.0 — 2026-11-06

Semantic architecture release: semantic model, identities, templates, ABI lowering, C++/Rust/Zig projections, error contracts, interface proxies, cache and the v0.3 benchmark baseline. See the `v0.3.0` git tag.
