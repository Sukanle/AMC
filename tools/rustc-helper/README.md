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
- generic declarations/instances, trait/impl metadata and `Result` error enums;
- Cargo workspace facts from `cargo metadata` when `--manifest-path` (or a discoverable `Cargo.toml`) is available.

## Requirements

- a stable Rust toolchain with the `rustc-dev` component (`rustup component add rustc-dev`);
- `rustc_driver` is an unstable interface even on stable, so the helper sets `RUSTC_BOOTSTRAP=1` at runtime and is pinned to the toolchain it was built against.

## Build

```sh
SYSROOT="$(rustc --print sysroot)"
HOST="$(rustc -vV | sed -n 's/^host: //p')"
RUSTC_BOOTSTRAP=1 rustc --edition 2021 tools/rustc-helper/src/main.rs \
  -L "$SYSROOT/lib" \
  -L "$SYSROOT/lib/rustlib/$HOST/lib" \
  -C "link-arg=-Wl,-rpath,$SYSROOT/lib" \
  -C "link-arg=-Wl,-rpath,$SYSROOT/lib/rustlib/$HOST/lib" \
  -o amc-rustc-helper
```

## Run

```sh
amc-rustc-helper --crate-name demo --edition 2021 \
  --source src/lib.rs --output demo.semantic.json

amc-rustc-helper --crate-name demo --edition 2021 \
  --source src/lib.rs --manifest-path Cargo.toml \
  --features default,fast --output demo.semantic.json
```

If `--output` is omitted, the document is written to stdout. `--manifest-path` is optional: without it the helper searches upwards from `--source` for a `Cargo.toml`. `--features` is additive; declared Cargo feature names are also recorded.

## AMC integration

`amc-rust` runs the helper when the import config declares it:

```toml
[rust]
helper = "path/to/amc-rustc-helper"
```

The helper fills missing `target` facts and validates the ones already present in `[target]`. AMC imports its types and functions into ABIX/Semantic IR, compares their layouts with the source parser and writes the semantic sidecar. If the helper fails and `[target]` provides the facts, AMC falls back to the existing parser/probe path with a warning.

The helper is not installed by the AMC CMake package yet; the v0.4.0 development tree builds it from this directory.
