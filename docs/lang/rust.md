# Rust

<p align="center">
  <a href="rust_zh.md">中文</a> · English
</p>

<details>
<summary>Contents</summary>

- [Role](#role)
- [Frontend](#frontend)
- [Generated projection](#generated-projection)
- [Ownership and errors](#ownership-and-errors)
- [Example](#example)
- [Limits and notes](#limits-and-notes)

</details>

Rust is ABIX's first non-C++ provider. `amc-rust` has a source-level frontend and a Rust backend. It links LLVM Support for filesystem and file I/O, but does not use Clang AST.

## Role

- `amc --describe-language rust` reports `frontend backend protocol=jsonl-v1`.
- The frontend reads `#[repr(C)]` structs/enums and `extern "C"` functions from `.rs` files and computes the C layout itself.
- The backend emits `amc_generated.rs` from an existing `.abix` artifact.
- `amc-rust` is a standalone provider process; it never changes ABIX IR.

## Frontend

`.abic.toml` selects `language = "rust"` and lists the source and exported symbols:

```toml
[[import]]
language = "rust"
files = ["src/lib.rs"]
symbols = ["DemoVec2", "demo_add"]

# Required: Rust layout is target-specific. `c_long_bits` is optional and
# defaults to the pointer width; Windows x64 targets should set it to 32.
[target]
arch = 1
os = 1
abi = 1
pointer_bits = 64
endianness = "little"

[[export]]
output = "demo.abix"
```

Without `[target] pointer_bits`, the Rust frontend fails explicitly instead of assuming a 64-bit Host layout.

The frontend is intentionally a preliminary extractor: it understands the subset that crosses an FFI boundary — `#[repr(C)]` structs/enums and `extern "C"` functions — and computes the same layout the C++ frontend gets from Clang. Generics, traits, lifetimes and bit-fields are out of scope; unsupported constructs are ignored rather than guessed.

### Explicit export handoff

`[[rust.expose]]` selects the functions that cross the boundary. The source function does not have to be `extern "C"`: AMC records the exported symbol and emits a `#[export_name = "<symbol>"] pub extern "C"` forwarding wrapper in the `[thunks]` output.

```toml
[[rust.expose]]
function = "internal_add"
as = "demo_add"          # optional; defaults to the source function name

[thunks]
output = "demo_thunks.rs"
```

Compile the generated file as a module in the same crate:

```rust
mod demo_thunks;
```

Rules:

- `function` must be a free function at crate root and visible to the generated module (`pub` or `pub(crate)`);
- methods with `self` are rejected in v0.3.3;
- a function that is already `extern "C"` and keeps its name needs no wrapper;
- a generated wrapper requires `[thunks] output`;
- `symbols` still selects types (and already-`extern "C"` functions); `[[rust.expose]]` is the explicit wrapper path for ordinary Rust functions.

### Ownership and error contracts

The generic `[[owned]]`, `[[contract]]` and `[[error]]` declarations apply to Rust functions too. They are resolved after the frontend records the ABI, so the Rust projection emits the same RAII and `Result` wrappers as the C++ path.

```toml
[[owned]]
type = "RustOwnedBuffer"
create = "rust_owned_create"
destroy = "rust_owned_destroy"

[[contract]]
function = "rust_owned_create"
returns = "owned"
release = "rust_owned_destroy"

[[error]]
function = "rust_div"
status = "return"
success = 0
value = 2
domain = 0x1003000000000001
```

The generated projection then contains:

- `OwnedRustOwnedBuffer` with `Drop` calling the declared destroy entry;
- `rust_div_result(...) -> Result<..., RustDivError>`.

The declarations reference types and functions that must be present in the module, either through `symbols` or `[[rust.expose]]`.

A provider source looks like this:

```rust
#[repr(C)]
pub struct DemoVec2 {
    pub x: i32,
    pub y: f64,
}

#[no_mangle]
pub extern "C" fn demo_add(a: i32, b: i32) -> i32 {
    a + b
}
```

## Generated projection

```bash
amc generate build/package.abix -l rust -o gen/
```

`amc_generated.rs` contains:

- `#[repr(C)]` records and enums;
- `unsafe extern "C"` declarations for the exported functions;
- `ABIX_*` constants for the type identity, size, align and field offsets;
- `const _: () = assert!(core::mem::size_of::<T>() == ...)` / `align_of` / `offset_of!` checks;
- an optional metadata-region module for run-time inspection.

C-ABI scalars project to `core::ffi::c_*` aliases (`c_int`, `c_double`, ...) derived from the ABI-normalised primitive identity, rather than fixed-width spellings, so the alias encodes the target's C ABI.

## Ownership and errors

The backend turns recorded contracts into Rust-native wrappers:

- an `owned` factory becomes an `Owned*` wrapper with `Drop`, `as_ref`, `as_ptr` and `into_raw`; the release entry point is the one recorded in the artifact, so Rust never frees foreign memory itself;
- an error boundary becomes a typed `Result<T, E>` wrapper such as `demo_div_result`;
- ABIX-STD span/string/optional/callback types project as typed records with their recorded layout.

The raw `extern "C"` calls remain `unsafe`; the wrappers reduce the unsafe surface without hiding the ABI.

## Example

```rust
#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;

fn main() {
    unsafe {
        assert_eq!(demo_add(2, 3), 5);

        let v = DemoVec2 { x: 4, y: 2.5 };
        assert_eq!(demo_scale(&v, 2.0), 13.0);

        let owned = OwnedDemoVec2::create(7, 1.5).expect("create");
        assert_eq!(owned.as_ref().x, 7);
        assert_eq!(demo_div_result(10, 2), Ok(5));
    }
}
```

The Rust file still needs to link the native library; the projection provides `#[link]` support through `ABIX_LIBRARY_NAME` when the `abix_link` option is enabled.

## Limits and notes

- The frontend is not a full `rustc` AST: only `#[repr(C)]` / `extern "C"` surfaces are extracted; generics, traits, lifetimes and bit-fields stay inside the module.
- `repr(C)` layout is validated against a generated rustc probe in the provider conformance test (`amc-rust-layout-probe-test`); the probe currently covers the Host target only.
- `repr(C, packed)` / `repr(C, packed(N))` and `repr(C)` unions are layout-validated, but their backend emission is not complete yet.
- `repr(transparent)` currently requires exactly one field; extra zero-sized fields are not modelled in v0.3.2.
- The recorded ABI is target-specific; compile the Rust consumer for the same target and C ABI as the provider.
- `isize` / `usize` keep their Rust spelling in the projection; `c_long` / `c_ulong` map to `core::ffi::c_long` / `c_ulong`. Other primitives use the ABI-width mapping.
- Panics and exceptions must not cross the boundary; the error boundary contract is the supported path.
- The generated file is ordinary Rust and can be wrapped in a safe crate API.
- `amc-rust` links LLVM Support but not Clang AST; its frontend computes the layout itself.
- v0.4.0 will move Rust semantic discovery to an independent rustc helper; the adapter contract is [amc.rust.semantic/1](../architecture/rust-adapter.md).
