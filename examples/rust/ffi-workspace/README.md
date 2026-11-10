# Rust FFI workspace example

A real Cargo workspace that uses AMC end to end:

```text
provider/   Rust cdylib; AMC parses its ordinary functions
consumer/   Rust binary; includes the generated bindings
```

The provider declares `Box<Widget>` ownership in ordinary Rust. AMC generates the `extern "C"` thunks, the owned-handle contract and the Rust projection. The consumer uses only `skl::abix::safe`, so it compiles with `#![deny(unsafe_code)]`.

## Run

```bash
./run.sh
```

The script:

1. runs `amc build` to emit `provider/src/generated/widget_thunks.rs`;
2. builds the provider cdylib with Cargo;
3. runs `amc generate -l rust` into `consumer/src/generated`;
4. builds and runs the consumer.

Set `AMC=/path/to/amc` when AMC is not on `PATH`. Generated files and `target/` are ignored.
