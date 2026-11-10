# Rust quick start

<p align="center">
  <a href="quickstart_ZH.md">中文</a> · English
</p>

This walkthrough turns an ordinary Rust source file into an ABIX artifact and a safe Rust consumer.

## 1. Describe the source

```toml
# demo.abic.toml
[package]
name = "demo"
version = "0.1"

[[import]]
language = "rust"
files = ["demo.rs"]
symbols = ["DemoVec2", "demo_add"]

[target]
arch = 0
os = 0
abi = 1
pointer_bits = 64
endianness = "little"

[[export]]
output = "demo.abix"
```

`pointer_bits` must be `64`; 32-bit and 16-bit targets are rejected. `[target]` is required because Rust layout is target-specific.

## 2. Build the artifact

```rust
// demo.rs
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

```bash
amc build -c demo.abic.toml -B build/demo
```

## 3. Generate the projection

```bash
amc generate build/demo/demo.abix -l rust -o gen/
```

## 4. Consume the projection

```rust
#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}

use skl::abix::*;

fn main() {
    assert_eq!(demo_add(2, 3), 5);
}
```

The consumer still links the native library. With the `abix_link` option the projection provides `#[link]` through `ABIX_LIBRARY_NAME`; otherwise add the link flags yourself.

## Real project example

`examples/rust/ffi-workspace/` is a Cargo workspace that runs `amc build`, Cargo, `amc generate` and a `#![deny(unsafe_code)]` consumer end to end:

```bash
cd examples/rust/ffi-workspace
AMC=/path/to/amc ./run.sh
```

## Next

- [Advanced: frontend, contracts and safe wrappers](advanced.md)
- [Rust overview](README.md)
