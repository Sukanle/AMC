# amc-rustc-helper

Minimal v0.4.0 Rust semantic adapter producer.

This is the first increment of the independent rustc helper described in `docs/architecture/rust-adapter.md`. It links the `rustc_driver` libraries in an isolated process and establishes:

- the process boundary between `amc-rust` and rustc;
- the stable `amc.rust.semantic/1` JSON envelope;
- target facts from the rustc target specification;
- crate/provenance facts.

This increment extracts `repr(C)` / `repr(transparent)` / packed / union records, enum layout and public `extern "C"` functions with their layout and signatures. `generics`, `traits` and `errors` remain empty and are filled by later revisions without changing the envelope.

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
```

If `--output` is omitted, the document is written to stdout.

## AMC integration

`amc-rust` runs the helper when the import config declares it:

```toml
[rust]
helper = "path/to/amc-rustc-helper"
```

The helper fills missing `target` facts and validates the ones already present in `[target]`. If the helper fails and `[target]` provides the facts, AMC falls back to the existing parser/probe path with a warning.

The helper is not installed by the AMC CMake package yet; the v0.4.0 development tree builds it from this directory.
