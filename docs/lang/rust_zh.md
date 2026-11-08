# Rust

<p align="center">
  <a href="rust.md">English</a> · 中文
</p>

<details>
<summary>目录</summary>

- [角色](#角色)
- [前端](#前端)
- [生成的投影](#生成的投影)
- [所有权与错误](#所有权与错误)
- [示例](#示例)
- [限制与说明](#限制与说明)

</details>

Rust 是 ABIX 的第一个非 C++ provider。`amc-rust` 包含源码级前端与 Rust 后端；它链接 LLVM Support 处理文件系统与文件 I/O，但不使用 Clang AST。

## 角色

- `amc --describe-language rust` 报告 `frontend backend protocol=jsonl-v1`。
- 前端从 `.rs` 文件中读取 `#[repr(C)]` struct/enum 与 `extern "C"` 函数，并自行计算 C 布局。
- 后端从已有的 `.abix` artifact 生成 `amc_generated.rs`。
- `amc-rust` 是独立的 provider 进程；它从不修改 ABIX IR。

## 前端

`.abic.toml` 选择 `language = "rust"`，并列出源码与导出符号：

```toml
[[import]]
language = "rust"
files = ["src/lib.rs"]
symbols = ["DemoVec2", "demo_add"]

# 必需：Rust 布局与 target 绑定。`c_long_bits` 可选：
# os=1（Windows）默认 32 位，其他支持的 target 跟随 pointer 宽度；
# 显式值始终优先。
[target]
arch = 0
os = 0
abi = 1
pointer_bits = 64
endianness = "little"

[[export]]
output = "demo.abix"
```

缺少 `[target] pointer_bits` 时，Rust 前端会明确失败，而不是默认使用 64 位 Host 布局。

前端刻意是一个初步提取器：它理解跨越 FFI 边界的那部分 Rust —— `#[repr(C)]` struct/enum 与 `extern "C"` 函数 —— 并计算与 C++ 前端从 Clang 得到的相同布局。泛型、trait、生命周期与位域不在范围内；不支持的构造会被忽略，而不是猜测。

### 显式导出交接

`[[rust.expose]]` 选择要跨边界的函数。源函数不必是 `extern "C"`：AMC 记录导出符号，并在 `[thunks]` 输出中生成 `#[export_name = "<symbol>"] pub extern "C"` 转发 wrapper。

```toml
[[rust.expose]]
function = "internal_add"
as = "demo_add"          # 可选，默认等于源函数名

[thunks]
output = "demo_thunks.rs"
```

把生成文件作为同一 crate 的模块编译：

```rust
mod demo_thunks;
```

规则：

- `function` 必须是 crate 根部的自由函数，并对生成模块可见（`pub` 或 `pub(crate)`）；
- 带 `self` 的方法在 v0.3.3 会被拒绝；
- 已经是 `extern "C"` 且保持原名的函数不需要 wrapper；
- 生成 wrapper 时必须提供 `[thunks] output`；
- `symbols` 仍用于选择类型（以及已 `extern "C"` 的函数）；`[[rust.expose]]` 是普通 Rust 函数的显式 wrapper 路径。

### 可选 rustc helper（v0.4.0 骨架）

v0.4.0 的 helper 进程已有一个最小骨架。它在独立进程中链接 `rustc_driver`，报告 target 与 crate 事实，并输出稳定的 `amc.rust.semantic/1` envelope；类型/函数数组由后续增量填充。构建需要带 `rustc-dev` 的 Rust toolchain。

```toml
[rust]
helper = "path/to/amc-rustc-helper"
```

配置后，`amc-rust` 会运行 helper 并使用其 target facts。如果 helper 失败且 `[target]` 已提供 `pointer_bits`、endianness 和 `c_long_bits`，AMC 会给出警告并回退到 parser/probe 路径。

### 所有权与错误契约

通用的 `[[owned]]`、`[[contract]]` 与 `[[error]]` 声明同样适用于 Rust 函数。它们在前端记录 ABI 之后解析，因此 Rust 投影会生成与 C++ 路径一致的 RAII 和 `Result` wrapper。

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

生成的投影包含：

- `OwnedRustOwnedBuffer`，`Drop` 调用声明的 destroy 入口；
- `rust_div_result(...) -> Result<..., RustDivError>`。

声明引用的类型与函数必须存在于模块中，可以通过 `symbols` 或 `[[rust.expose]]` 选择。

一个 provider 源码如下：

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

## 生成的投影

```bash
amc generate build/package.abix -l rust -o gen/
```

`amc_generated.rs` 包含：

- `#[repr(C)]` 记录与枚举；
- 导出函数的 `unsafe extern "C"` 声明；
- 类型身份、大小、对齐与字段 offset 的 `ABIX_*` 常量；
- `const _: () = assert!(core::mem::size_of::<T>() == ...)` / `align_of` / `offset_of!` 检查；
- 可选的 metadata region 模块，用于运行时检查。

C ABI 标量会投影为 `core::ffi::c_*` 别名 (`c_int`、`c_double` 等)，这些别名由 ABI 归一化后的 primitive 身份推导，而不是固定宽度拼写，因此别名本身编码了目标平台的 C ABI。

## 所有权与错误

后端把记录的契约转换成 Rust 原生包装：

- `owned` 工厂变成带 `Drop`、`as_ref`、`as_ptr`、`into_raw` 的 `Owned*` 包装；释放入口就是 artifact 中记录的那个，因此 Rust 不会自行释放外部内存；
- 错误边界变成带类型的 `Result<T, E>` 包装，例如 `demo_div_result`；
- ABIX-STD 的 span/string/optional/callback 类型按记录的布局投影为带类型的记录。

原始 `extern "C"` 调用仍然是 `unsafe` 的；包装缩小了 unsafe 表面积，但没有掩盖 ABI。

## 示例

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

Rust 文件仍需要链接原生库；当启用 `abix_link` 选项时，投影会通过 `ABIX_LIBRARY_NAME` 提供 `#[link]` 支持。

## 限制与说明

- 前端不是完整的 `rustc` AST：只提取 `#[repr(C)]` / `extern "C"` 表面；泛型、trait、生命周期与位域留在模块内部。
- `repr(C)` 布局通过 provider conformance 测试中的 rustc probe 验证（`amc-rust-layout-probe-test`）；probe 目前只覆盖 Host target。
- `repr(C, packed)` / `repr(C, packed(N))` 和 `repr(C)` union 只完成布局验证；backend emission 暂缓到 v0.4.0，与一级 union/packed/transparent 表示模型一起落地。v0.3.x 不引入中间态 `TypeRepr` section。
- `repr(transparent)` 目前要求恰好一个字段；额外的零大小字段在 v0.3.x 尚未建模。
- 记录的 ABI 是 target 相关的；Rust 消费方必须按与 provider 相同的 target 与 C ABI 编译。
- `isize` / `usize` 在投影中保留 Rust 拼写；`c_long` / `c_ulong` 映射为 `core::ffi::c_long` / `c_ulong`。其他 primitive 继续使用 ABI 宽度映射。
- panic 与异常不得穿越边界；错误边界契约是受支持的路径。
- 生成文件是普通 Rust，可以在外层再封装成安全的 crate API。
- `amc-rust` 链接 LLVM Support 但不使用 Clang AST；它的前端自行计算布局。
- v0.4.0 会将 Rust 语义发现迁移到独立 rustc helper；adapter 契约见 [amc.rust.semantic/1](../architecture/rust-adapter_zh.md)。
