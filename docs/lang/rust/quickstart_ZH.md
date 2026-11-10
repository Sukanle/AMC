# Rust 快速开始

<p align="center">
  <a href="quickstart.md">English</a> · 中文
</p>

本流程把一个普通 Rust 源文件变成 ABIX artifact 和安全 Rust 消费者。

## 1. 描述源码

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

`pointer_bits` 必须为 `64`；32 位与 16 位目标会被拒绝。`[target]` 是必需的，因为 Rust 布局与 target 绑定。

## 2. 构建 artifact

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

## 3. 生成投影

```bash
amc generate build/demo/demo.abix -l rust -o gen/
```

## 4. 使用投影

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

消费者仍需要链接原生库。启用 `abix_link` 选项时，投影会通过 `ABIX_LIBRARY_NAME` 提供 `#[link]`；否则需要自行添加链接参数。

## 真实项目示例

`examples/rust/ffi-workspace/` 是一个 Cargo workspace，端到端运行 `amc build`、Cargo、`amc generate` 和 `#![deny(unsafe_code)]` 消费者：

```bash
cd examples/rust/ffi-workspace
AMC=/path/to/amc ./run.sh
```

## 下一步

- [高级特性：前端、契约与安全包装](advanced_ZH.md)
- [Rust 概览](README_ZH.md)
