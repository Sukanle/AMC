# Rust

<p align="center">
  <a href="README_ZH.md">中文</a> · English
</p>

Rust is ABIX's first non-C++ provider. `amc-rust` has a source-level frontend and a Rust backend. It links LLVM Support for filesystem and file I/O, but does not use Clang AST.

## What you get

- `amc_generated.rs` with `#[repr(C)]` records, `extern "C"` entries and `const assert!` layout checks.
- `Owned*`, `OwnerOf*`, `OwnedString*` and `OwnedSpan*` RAII wrappers that release through the recorded entry point.
- A `pub mod safe` facade that a consumer crate can use with `#![deny(unsafe_code)]`.
- Typed `Result` wrappers for recorded error boundaries.

## Role

- `amc --describe-language rust` reports `frontend backend protocol=jsonl-v1`.
- The frontend reads `#[repr(C)]` structs/enums and `extern "C"` functions from `.rs` files and computes the C layout itself.
- The backend emits `amc_generated.rs` from an existing `.abix` artifact.
- `amc-rust` is a standalone provider process; it never changes ABIX IR.

## Owned handles and safe borrowing

The generated projection validates owner handles and accepts Rust borrows for source `&T` / `&mut T` parameters:

```rust
#![deny(unsafe_code)]
use skl::abix::safe::*;

let owner = box_widget_make(7)?;
assert_eq!(box_widget_value(owner), 7);
```

Raw pointers, `from_raw` / `into_raw` and `LeaseGuard::acquire` stay as explicit escape hatches. See [advanced topics](advanced.md) for lease hosts, container contracts and the full generated surface.

## Next

- [Quick start](quickstart.md)
- [Advanced: frontend, contracts and safe wrappers](advanced.md)
- [Real Cargo workspace example](../../../examples/rust/ffi-workspace/README.md)
