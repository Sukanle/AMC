# amc-rustc-helper

v0.4.0 Rust semantic adapter producer.

This is the independent rustc helper described in `docs/architecture/rust-adapter.md`. It links the `rustc_driver` libraries in an isolated process and establishes:

- the process boundary between `amc-rust` and rustc;
- the stable `amc.rust.semantic/1` JSON envelope;
- target facts from the rustc target specification;
- crate/provenance facts.

The helper now extracts:

- `repr(C)` / `repr(transparent)` / packed / align / union / enum records, including nested modules, generic declarations and concrete generic instances;
- public `extern "C"` functions, including associated functions in `impl` blocks;
- generic declarations/instances, trait/impl metadata, `Result` error enums and lifetimes;
- trait objects and async functions as semantic metadata (they are not ABI-projectable by default);
- Cargo workspace facts from `cargo metadata` when `--manifest-path` (or a discoverable `Cargo.toml`) is available.

## Requirements

- a stable Rust toolchain with the `rustc-dev` component (`rustup component add rustc-dev`);
- `rustc_driver` is an unstable interface even on stable, so the helper sets `RUSTC_BOOTSTRAP=1` at runtime and is pinned to the toolchain it was built against.

## Build

```sh
cd tools/rustc-helper
cargo build --release
# binary: target/release/amc-rustc-helper
```

`build.rs` asks the active rustc for its sysroot and host triple, then adds the `rustc-dev` search paths and rpath entries. `RUSTC_BOOTSTRAP=1` is set by `.cargo/config.toml`; the helper also sets it at runtime for `rustc_driver`. Build with a toolchain that has the `rustc-dev` component, and keep the helper pinned to that toolchain.

## Run

```sh
amc-rustc-helper --crate-name demo --edition 2021 \
  --source src/lib.rs --output demo.semantic.json

amc-rustc-helper --crate-name demo --edition 2021 \
  --source src/lib.rs --manifest-path Cargo.toml \
  --features default,fast --output demo.semantic.json

amc-rustc-helper --crate-name demo --edition 2021 \
  --source src/lib.rs --instantiate 'Wrapper=u64' --output demo.semantic.json
```

If `--output` is omitted, the document is written to stdout. `--manifest-path` is optional: without it the helper searches upwards from `--source` for a `Cargo.toml`. `--features` is additive; declared Cargo feature names are also recorded. `--instantiate` is repeatable and requests one concrete local generic instance for primitive arguments (`Template=arg1,arg2`).

## AMC integration

`amc-rust` runs the helper when the import config declares it:

```toml
[rust]
helper = "path/to/amc-rustc-helper"
```

The helper fills missing `target` facts and validates the ones already present in `[target]`. AMC imports its types and functions into ABIX/Semantic IR, copies generics/traits/errors/lifetimes/trait objects/async functions into the `rust` extension of the semantic sidecar, compares layouts with the source parser and writes the sidecar. If the helper fails and `[target]` provides the facts, AMC falls back to the existing parser/probe path with a warning.

`[rust] features` is forwarded as `--features`; an `[[instantiate]]` entry is forwarded as `--instantiate` for Rust generic selection.

The helper is not installed by the AMC CMake package yet; the v0.4.0 development tree builds it from this directory.
