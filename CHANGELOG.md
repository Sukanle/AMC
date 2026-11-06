# Changelog

## v0.3.1 — 2026-11-07

### Added

- Rust FFI surface now reads `[target]` facts (`arch`, `os`, `abi`, `pointer_bits`,
  `endianness`, optional `c_long_bits`) and records them in the ABI module.
- `amc-rust-layout-probe-test`: compares AMC layout with a generated rustc
  `size_of` / `align_of` / `offset_of!` probe.
- Rust parity fixtures for pointer-sized primitives, fixed arrays, type aliases,
  non-default enum representation and packed structs.
- `repr(C, packed)` layout validation against rustc.
- Void-returning `extern "C"` functions are now recorded instead of skipped.
- Host target validation against `rustc --print cfg`.

### Changed

- **Breaking for Rust imports:** a Rust `.abic.toml` now requires a `[target]`
  section with `pointer_bits`. Existing configs must add:

  ```toml
  [target]
  arch = 1
  os = 1
  abi = 1
  pointer_bits = 64
  endianness = "little"
  ```

- Rust `pointer`, `isize`, `usize`, `c_long` and `c_ulong` are target-aware; the
  old hard-coded 8-byte assumption is removed.
- Project and provider versions are aligned to `0.3.1`; the MCP server version
  is aligned as well.

### Fixed

- `#[repr(C, packed)]` field offsets are now packed; previously only the overall
  alignment was reduced.
- `#[repr(C, packed)]` layout is validated against rustc.
- Rust functions returning `()` are no longer silently dropped.

### Known limitations

- The layout probe is Host-only. Cross-target reflection is deferred.
- `repr(C, packed)` is layout-validated but backend emission for packed types is
  not implemented yet; it is not advertised as a complete projection feature.
- Primitive aliases such as `isize` / `c_long` may share the ABI type with
  `i32` / `i64`; source-spelling retention is a follow-up.
- Windows x64 users must set `c_long_bits = 32` explicitly when the target uses
  the Windows C ABI.
- Advanced Rust semantics (trait, associated type, trait object, complex
  lifetimes, Cargo workspace) remain v0.4.0 work.

### Validation

- CTest: 28/28 passed.
- `boundary --strict`: passed.
- `amc-rust-test`: passed.
- `amc-rust-layout-probe-test`: passed.

## v0.3.0 — 2026-11-06

Semantic architecture release: semantic model, identities, templates, ABI
lowering, C++/Rust/Zig projections, error contracts, interface proxies, cache
and the v0.3 benchmark baseline. See the `v0.3.0` git tag.
