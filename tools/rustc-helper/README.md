# amc-rustc-helper

Minimal v0.4.0 Rust semantic adapter producer.

This is the first increment of the independent rustc helper described in
`docs/architecture/rust-adapter.md`. It establishes:

- the process boundary between `amc-rust` and rustc;
- the stable `amc.rust.semantic/1` JSON envelope;
- target facts from the installed rustc (`-vV` / `--print cfg`);
- crate/provenance facts.

The `types`, `functions`, `generics`, `traits` and `errors` arrays are
intentionally empty in this increment. Later revisions fill them from rustc's
semantic APIs without changing the envelope.

## Build

```sh
rustc --edition 2021 tools/rustc-helper/src/main.rs -o amc-rustc-helper
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

The helper fills missing `target` facts and validates the ones already present
in `[target]`. If the helper fails and `[target]` provides the facts, AMC
falls back to the existing parser/probe path with a warning.

The helper is not installed by the AMC CMake package yet; the v0.4.0
development tree builds it from this directory.
