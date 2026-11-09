# Changelog

## Unreleased — v0.4.0

### Added

- Rust semantic IR extension: `.semantic` now carries a `rust` object with generics, traits/impls, `Result` errors, lifetimes, trait objects and async functions. ABIX-compatible types/functions remain in the ABI view.
- Explicit Rust generic selection: `--instantiate Template=arg1,arg2` and `[[instantiate]]` emit a concrete local instance on request instead of enumerating monomorphizations.
- `[rust] features` in `.abic.toml` is forwarded to the helper; without a Cargo manifest the helper still records CLI features.
- Rust `[[error]]` contracts now reuse a matching helper error enum as an error set (name and non-zero variant codes) while the explicit contract still selects the domain, success value and value slot.
- `tools/rustc-helper/`: `amc-rustc-helper` is now a real `rustc_driver` semantic producer. It emits the `amc.rust.semantic/1` envelope and extracts `repr(C)` / `repr(transparent)` / packed / align / union / enum records from nested modules, concrete generic instances, public `extern "C"` functions (including `impl` associated functions), generic declarations, trait/impl metadata and `Result` error enums. Building it requires `rustc-dev`.
- `[rust] helper` config: `amc-rust` imports the helper's types and functions into ABIX/Semantic IR, cross-checks their layouts against the source parser, and writes the semantic sidecar. Parser errors for syntax the helper already proved are downgraded to warnings. The parser/probe remains the fallback when the helper is unavailable.
- Cargo metadata integration: the helper fills crate name, edition and declared features from `cargo metadata` through `--manifest-path` (or a manifest discovered above `--source`), with additive `--features`.
- Cargo workspace facts: the helper records package id, manifest path, workspace root, target directory, workspace/default members and dependencies (kind, requirement, optional, path). `[rust] manifest` and `[rust] target` are forwarded to the helper, and the facts persist under `.semantic` `rust.cargo`.
- Explicit generic instances now accept primitives, non-generic local types and const-generic decimal integer literals. Unknown names, extra arguments and malformed const values fail closed instead of producing a guessed instance.
- Standard-container facts: `Option<T>`, `Box<T>`, `String` and `Vec<T>` occurrences are recorded under `.semantic` `rust.containers`. Selecting one for the ABI fails with a diagnostic that asks for an explicit `[[contract]]` instead of assuming a niche layout or ownership.
- `AMC_BUILD_RUST_HELPER=ON` builds `tools/rustc-helper` with Cargo into the build tree (scoped `RUSTC_BOOTSTRAP=1`) and installs the binary next to the AMC tools. Tests keep building the helper on demand when the option is off.
- `amc-rust-helper-test`: builds the helper with Cargo, validates generics/traits/errors/cargo metadata, cross-validates layout with a rustc `size_of`/`align_of`/`offset_of!` probe, and checks the AMC import path.
- `tools/rustc-helper` is now a Cargo project (`Cargo.toml`, `Cargo.lock`, `build.rs`). `clap` handles the command line, `serde_json` emits the envelope, and `cargo_metadata` reads workspace facts instead of hand-written parsers.
- Rust projection: raw FFI declarations move into a `raw` module; generated wrappers keep the source names, are safe `pub fn` for pure value types and `pub unsafe fn` for pointer/owner contracts, and funnel every raw call through the generated `amc_ffi!` macro. `Owned*` and typed `Result` wrappers use the same macro.
- `tools/imhex/`: ImHex Pattern Language definitions for ABIX v4 `.abix` artifacts and ABIX Metadata Regions, including the section directory, all record tables, extension contracts, string names and the assembly-level `.abix.metadata` embedding.
- `tools/run.py` now keeps AMC test artifacts under `build/tmp` by default (`--artifact-dir` overrides it). It exports `AMC_ARTIFACT_DIR` and points `TMPDIR`/`TMP`/`TEMP` there, so `.abix` files, native libraries and test executables survive the test run for inspection.

### Breaking

- Adopt the ABIX v0.4.0 explicit ABIX-STD layouts for `abix_optional`, `abix_result` and `abix_str`. `AMC_ABIX_REF` is now v0.4.0 and `find_package(ABIX 0.4.0)` is required.
- Old v0.3.x `.abix` artifacts and generated bindings are not compatible; regenerate them with this tree.

### Changed

- The `amc` CLI and the `amc-cpp` / `amc-rust` / `amc-zig` providers now parse arguments with LLVM `CommandLine`. Each command is a registered subcommand with typed options and automatic `--help` / `--version`; the hand-written argv loops, the manual `--format=` splitting and the manual usage text are gone. Command spellings are registered once through MICS `SKL_RFS_ENUM`, and an invocation without a command prints LLVM's subcommand overview. `--error-format` and every per-command `--format` are real LLVM options whose callbacks select the error envelope; only `--error-format` is relocated past the subcommand token because LLVM requires the subcommand first, so parse failures still use the selected envelope.
- Trait objects (`&dyn Trait` / `*const dyn Trait`) are rejected in function signatures and record fields. They are fat pointers, so AMC no longer lowers them as thin pointers without an explicit contract.
- Rust semantic reload now restores async functions as well as the other language extensions.
- C++ namespaces are unified as `skl::amc` (generated code: `skl::amc::generated`) and C++ enum values use `UPPER_SNAKE_CASE`.
- Enum names and parsing for Core/Semantic/IR/Diagnostics/Compilation/Artifact are driven by shared MICS `SKL_RFS_ENUM` tables instead of local switches.
- `evaluate_optional` / `evaluate_result` / `evaluate_string_view` accept the v0.4.0 explicit `reserved` layouts and reject a malformed reserved field.
- Synthesized StringView boundaries and C++ thunks include the explicit reserved word; error-boundary value indices follow the new `status, reserved, value` layout.
- Rust/Zig/C++ interop fixtures use the explicit layouts.

### Validation

- Linux CTest: 35/35 passed.
- `amc-rust-helper-test`: 131 checks, including Cargo workspaces, const/local generic instances, container facts, trait-object rejection and rustc layout probes.
- The rustc frontend work continues separately; this entry records the ABIX lockstep only.

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
- Windows: the expose interop test now skips the Zig interface-proxy consumer when Zig is unavailable, matching the existing Zig handle/STD consumer guards.

### Compatibility

- ABIX v0.4.0's breaking `abix_optional` / `abix_result` / `abix_str` update is not part of this release. AMC v0.4.0 will perform the lockstep migration and regenerate all artifacts.

### Known limitations

- cross-target probe remains future work; the Host layout probe is stable.
- `packed` / `transparent` / `union` backend emission is deferred to v0.4.0, together with the first-class representation model. v0.3.x keeps layout validation only.

### Validation

- Linux: CTest 34/34 passed.
- `amc-boundary-contracts` and `amc-boundary-edges --strict`: passed.
- `amc-lease-interop-test`, `amc-rust-expose-test`, `amc-rust-contract-test`, `amc-rust-clong-test`: passed.
- `amc-provider-path-test`: passed.
- Install smoke: `cmake --install` followed by `amc build` invoked through PATH.
- Windows/macOS: passed (validated before tagging).

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
