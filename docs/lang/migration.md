# Migration cost preview

<p align="center">
  <a href="migration_ZH.md">中文</a> · English
</p>

This page lists the source files **you write** and the files **AMC generates**. AMC compiles the interface surface, not the whole program, so migration means describing and adapting the boundary, not porting the implementation.

## 1. Migrating your own source (cmdly-style)

Use this path when you own the provider source and its build system. The provider keeps its implementation; you only expose the ABI surface.

### Files you write

| File                                             | Purpose                                                                                    | Required                      |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------ | ----------------------------- |
| Provider source (`src/lib.rs`, `*.hpp`, `*.cpp`) | `#[repr(C)]` records and `extern "C"` functions, or C++ declarations selected by `symbols` | yes                           |
| `amc.abic.toml`                                  | `[[import]]` files/symbols, `[target]`, `[[export]]` and optional contracts                | yes                           |
| Provider include (`include!("*_thunks.rs")`)     | Compiles the generated wrappers into the provider crate                                    | only with `[[rust.expose]]`   |
| Consumer source                                  | Calls the generated projection                                                             | yes                           |
| Build files (`Cargo.toml`, `CMakeLists.txt`)     | Builds the provider and consumer, and runs `amc build` / `amc generate`                    | yes                           |
| Consumer `build.rs` or link flags                | Locates and links the provider native library                                              | when the consumer is separate |

### AMC generates

- `.abix`, `.semantic` and `.lowering` artifacts;
- `amc_generated.rs`, `amc_generated.hpp` or `amc_generated.zig`;
- `amc_abi_check.hpp` or `const assert!` / `comptime` layout checks;
- `*_thunks.rs` when `[[rust.expose]]` wraps ordinary Rust functions;
- `OwnerOf*`, `Owned*`, `OwnedString*`, `OwnedSpan*`, `Result` wrappers and `pub mod safe`.

### Minimal layout

```text
my-project/
├── amc.abic.toml
├── provider/
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs          # your implementation + exported surface
│       └── generated/      # AMC thunks (when Rust expose is used)
└── consumer/
    ├── Cargo.toml
    └── src/main.rs         # uses the generated projection
```

### Cost notes

- You do not hand-write `extern "C"` declarations, owner destroy trampolines or layout assertions.
- Expect to adjust visibility and signatures so the exported surface is representable in the ABI.
- A Cargo workspace example is in [`examples/rust/ffi-workspace/`](../../examples/rust/ffi-workspace/README.md).

## 2. Migrating an existing DLL (OpenCV-style)

Use this path when the native library already exists and you cannot change its build or C++ ABI. The missing piece is a stable C boundary.

### Files you write

| File                      | Purpose                                                                                     |
| ------------------------- | ------------------------------------------------------------------------------------------- |
| `*_shim.cpp` / `*_shim.c` | C ABI over the existing API: opaque handles, lifetime, bounds checks and exception catching |
| `*_shim_ffi.rs`           | Rust `extern "C"` declarations for the shim                                                 |
| `*_bridge.rs`             | `#[repr(C)]` handle plus ordinary Rust functions; `Box<T>` expresses ownership              |
| `amc.abic.toml`           | `[[adapt]]`, `[[rust.expose]]`, `[thunks]`, `[target]` and `[[export]]`                     |
| Consumer source           | Uses the generated safe facade                                                              |
| Build/link files          | Compiles the shim, links the original library and links the generated provider              |

### AMC generates

- the owner handle (`abix_owner_<T>`) and its destroy trampoline;
- `OwnerOf<T>` with `as_ref` / `as_mut`, `Drop` and the safe factory;
- `extern "C"` thunks for the Rust bridge;
- `amc_generated.*` and the language-specific layout checks;
- a safe facade for borrowed `&T` / `&mut T` parameters and `&str` / `&[T]` views.

### Why a shim is required

- C++ classes, templates, overloads and exceptions do not have a stable cross-language ABI.
- OpenCV 5 removed the legacy C API, so a small `cv::Mat` shim is the simplest bridge.
- AMC projects the ABI you expose; it cannot infer an existing C++ object model from a header alone.

### Minimal layout

```text
my-bridge/
├── amc.abic.toml
├── native_shim.cpp          # C ABI over the existing DLL
├── src/
│   ├── shim_ffi.rs          # extern "C" declarations
│   ├── bridge.rs            # #[repr(C)] handle + Rust functions
│   └── generated/           # AMC thunks
└── consumer/
    └── src/main.rs          # safe facade consumer
```

### Cost notes

- The one-time cost is the shim and the handle/ownership mapping; the consumer stays `unsafe`-free.
- Keep the C ABI small: create/destroy, a few operations, and status/error returns.
- Catch native exceptions inside the shim; do not let them unwind into Rust.
- A working example is in [`tests/interop/opencv/`](../../tests/interop/opencv/README.md).

## What AMC never asks you to write

- `.abix`, `.semantic` or `.lowering` records;
- generated function declarations or manual `extern` blocks for the exported surface;
- `static_assert` / `const assert!` / `comptime` layout checks;
- owner destroy trampolines or `Drop` implementations for foreign memory;
- the safe facade.

## Effort at a glance

| Scenario        | One-time work                                           | Ongoing work                                |
| --------------- | ------------------------------------------------------- | ------------------------------------------- |
| Your own source | Annotate exports, write `.abic.toml`, wire the consumer | Regenerate after interface changes          |
| Existing DLL    | Write the C shim and the Rust bridge, map ownership     | Maintain the shim when the native API moves |

Start with the [Rust quick start](rust/quickstart.md) or the language-specific [quick start](cpp/quickstart.md) / [Zig quick start](zig/quickstart.md).
