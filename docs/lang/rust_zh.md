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

# 必需：Rust 布局与 target 绑定。`pointer_bits` 必须为 64；
# 32 位和 16 位目标不受支持。`c_long_bits` 可选：
# os=1（Windows）默认 32 位，其他 64 位 target 跟随 pointer 宽度；
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

缺少 `[target] pointer_bits` 时，Rust 前端会明确失败，而不是默认使用 64 位 Host 布局。 `pointer_bits` 必须为 `64`：32 位和 16 位目标会以显式诊断被拒绝（见[平台支持](../architecture/platform-support_zh.md)）。

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

### 可选 rustc helper（v0.4.0）

v0.4.0 的 helper 进程在独立进程中链接 `rustc_driver`。它报告 target/crate 事实，遍历嵌套 module 和 impl，提取 `repr(C)` / `repr(transparent)` / packed / union 记录、具体泛型实例、public `extern "C"` 函数、泛型声明、traits/impls 和 `Result` error enum，并输出稳定的 `amc.rust.semantic/1` envelope。存在 Cargo manifest 时还会从 `cargo metadata` 填充 workspace 与 feature 事实。AMC 将 helper 的 types/functions 导入 ABIX/Semantic IR，并与源码 parser 交叉校验布局。构建需要带 `rustc-dev` 的 Rust toolchain。`cmake -DAMC_BUILD_RUST_HELPER=ON` 会用 Cargo 把 `tools/rustc-helper/` 构建到 build 目录并安装到 `amc` 旁边；不开启该选项时，helper 测试会按需自行构建。

```toml
[rust]
helper = "path/to/amc-rustc-helper"
manifest = "path/to/Cargo.toml"   # 可选，显式 Cargo manifest
target = "x86_64-unknown-linux-gnu"  # 可选，rustc target triple
edition = "2024"                    # 可选，2015 | 2018 | 2021 | 2024
features = ["default", "std"]

[[instantiate]]
template = "Wrapper"
args = ["u64"]
as = "WrapperU64"
```

`[rust] manifest` 会作为 `--manifest-path` 转发，使 helper 能通过 `cargo metadata` 解析 workspace 成员、默认成员、依赖和 target directory。`[rust] target` 会作为 `--target` 转发，用于选择布局和 target facts 的 rustc target。`[rust] edition` 选择 rustc edition（默认 `2021`；存在 manifest 且未设置该键时使用 Cargo package edition）；不支持的值会在 helper 启动前失败。`[rust] features` 会作为 `--features` 转发给 helper。每个 `[[instantiate]]` 条目会作为 `--instantiate` 转发，强制输出一个本地具体泛型实例；类型参数可以是 primitive 或非泛型本地类型，const generic 参数使用十进制整数字面量；helper 仍不会枚举所有 monomorphization。

配置后，`amc-rust` 会运行 helper 并使用其 target facts。如果 helper 失败且 `[target]` 已提供 `pointer_bits`、endianness 和 `c_long_bits`，AMC 会给出警告并回退到 parser/probe 路径。

`Option<T>`、`Box<T>`、`String`、`Vec<T>` 会作为语义事实记录，而不会被当作隐式 ABI 布局。选择包含这些类型的函数或字段时必须提供显式 `[[contract]]`；否则构建会以点名容器的诊断失败，而不是静默猜测所有权或缓冲区表示。trait object 也会被同样拒绝，因为 `&dyn Trait` 是 fat pointer，没有显式契约时不能当作普通指针降级。

### 容器契约（`String`、`Vec<T>`、`Option<T>`）

Rust 容器不能裸跨边界。通过 `[[adapt]]` 声明映射，并用 `[[rust.expose]]` 暴露普通 Rust 函数：

```toml
[[adapt]]
native = "String"
target = "abix_str_utf8"

[[adapt]]
native = "Vec"
target = "abix_span_i32"
element = "i32"

[[adapt]]
native = "Option"
target = "abix_optional_i32"
element = "i32"

[[rust.expose]]
function = "container_string_len"
as = "container_string_len"

[thunks]
output = "container_thunks.rs"
```

AMC 会合成边界 record，在 thunk 源码中生成对应的 `#[repr(C)]` 结构，并生成 marshal 代码：

- `String` / `&str` → `abix_str_utf8`（`&str` 零拷贝；`String` 复制为 owned 值）；
- `Vec<T>` → `abix_span_<T>`（`to_vec()`；使用 `native = "&[T]"` 可获得零拷贝 slice）；
- `Option<T>` → `abix_optional_<T>`（wrapper 侧用 `MaybeUninit`，仅在判别为 `Some` 时读取值）。

`Option<T>` 返回值作为 value contract 支持：wrapper 把 `Some` 转成 `{ discriminant: 1, value: MaybeUninit::new(value) }`，把 `None` 转成 `{ discriminant: 0, value: MaybeUninit::uninit() }`。`Box<dyn Trait>` 是 fat pointer，会明确拒绝；请使用 opaque handle 契约。

`Box<T>` 可以直接映射到 ABIX owner handle `abix_owner_<T>`：wrapper 把 `Box::into_raw` 存入 `owner.object`，设置 `mode = owned`，并把 `Box::from_raw` trampoline 放入 `owner.destroy`；消费方通过 handle 释放。旧的显式 `*mut T` + `[[owned]]` 路径仍保留：

```toml
[[adapt]]
native = "Box"
target = "abix_owner_Widget"
```

`Box<T>` 走显式 owned-handle 路径：声明指针 target、`[[owned]]` release policy 以及每个交互的 `[[contract]]` 模式。AMC 不会自动合成 create/destroy。

```toml
[[adapt]]
native = "Box"
target = "*mut Widget"

[[owned]]
type = "Widget"
create = "widget_make"
destroy = "widget_destroy"

[[contract]]
function = "widget_make"
returns = "owned"
release = "widget_destroy"

[[contract]]
function = "widget_use"
params = ["owned"]
```

wrapper 会把返回的 `Box<T>` 转成 `Box::into_raw`，把 `Box<T>` 参数转成 `Box::from_raw`。create/destroy 默认仍由生产者显式提供；机械配对可以选择自动生成：

```toml
[[owned]]
type = "Widget"
create = "auto"
destroy = "auto"

[[contract]]
function = "widget_make"
returns = "owned"
release = "auto"
```

`create = "auto"` 生成 `fn(T) -> *mut T` 工厂（`Box::new`），`destroy = "auto"` 生成 `Box::from_raw` + drop，`release = "auto"` 复用匹配 `[[owned]]` 的 destroy 签名。owned `String`/`Vec` 返回值映射到 `abix_owned_str_utf8` / `abix_owned_span_<T>`：生成的值包含 borrowed view 和 `abix_owner_handle`，其 destroy trampoline 负责释放 Rust 分配。`Option<T>` 返回值是 value contract。`Vec<T>`/`Option<T>` 的元素可以是 primitive 或本地 `#[repr(C)]` record；owned `Vec<T>` marshal 要求 `T: Clone`。

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
- 导出函数的 `unsafe extern "C"` 声明所在的 `raw` 模块；
- 生成的函数包装与拥有唯一 `unsafe` 调用点的 `amc_ffi!` 宏；
- 类型身份、大小、对齐与字段 offset 的 `ABIX_*` 常量；
- `const _: () = assert!(core::mem::size_of::<T>() == ...)` / `align_of` / `offset_of!` 检查；
- 可选的 metadata region 模块，用于运行时检查。

C ABI 标量会投影为 `core::ffi::c_*` 别名 (`c_int`、`c_double` 等)，这些别名由 ABI 归一化后的 primitive 身份推导，而不是固定宽度拼写，因此别名本身编码了目标平台的 C ABI。

## 所有权与错误

后端把记录的契约转换成 Rust 原生包装：

- `owned` 工厂变成带 `Drop`、`as_ref`、`as_ptr`、`into_raw` 的 `Owned*` 包装；释放入口就是 artifact 中记录的那个，因此 Rust 不会自行释放外部内存；
- 错误边界变成带类型的 `Result<T, E>` 包装，例如 `demo_div_result`；
- ABIX-STD 的 span/string/optional/callback 类型按记录的布局投影为带类型的记录。

## 安全包装

原始声明位于 `abix::raw`。生成的包装保留源码函数名，并分成两档：

- **安全** `pub fn`：所有参数与返回值都是纯 ABI 值（primitive、enum、不含裸指针的 record）。调用时不需要 `unsafe`；
- **`pub unsafe fn`**：参数或返回值包含裸指针、owner 或含指针类型。unsafe 契约现在显式出现在调用点，而不是隐藏在 `extern` 块中。

每个包装都通过唯一生成的宏执行真正的 FFI 调用：

```rust
macro_rules! amc_ffi {
    ($function:path($($arg:expr),* $(,)?)) => {
        unsafe { $function($($arg),*) }
    };
}
```

`Owned*` RAII 方法和带类型的 `Result` 包装也使用 `amc_ffi!`，因此生成的 unsafe 块集中在一处。ABI 本身仍然显式：高级消费者仍可直接使用 `raw` 模块和 `ABIX_*` 常量。

## 示例

```rust
#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}

use skl::abix::*;

fn main() {
    // 纯值入口是安全包装。
    assert_eq!(demo_add(2, 3), 5);

    // 指针与 owner 操作保留显式 unsafe 契约。
    unsafe {
        let v = DemoVec2 { x: 4, y: 2.5 };
        assert_eq!(demo_scale(&v, 2.0), 13.0);
    }

    let owned = OwnedDemoVec2::create(7, 1.5).expect("create");
    assert_eq!(owned.as_ref().x, 7);
    assert_eq!(demo_div_result(10, 2), Ok(5));
}
```

Rust 文件仍需要链接原生库；当启用 `abix_link` 选项时，投影会通过 `ABIX_LIBRARY_NAME` 提供 `#[link]` 支持。

## 限制与说明

- 源码 parser 不是完整的 `rustc` AST：只提取 `#[repr(C)]` / `extern "C"` 表面；泛型、trait、生命周期与位域留在模块内部。可选的 helper 通过 rustc 视图补齐泛型实例、trait、生命周期和标准容器。
- `repr(C)` 布局通过 provider conformance 测试中的 rustc probe 验证（`amc-rust-layout-probe-test`）；probe 目前只覆盖 Host target。
- `repr(C, packed)` / `repr(C, packed(N))` 和 `repr(C)` union 只完成布局验证；backend emission 延后到 v0.4.x/v0.5.0，与一级 union/packed/transparent 表示模型一起落地。v0.4.0 不生成 packed/union 绑定。
- `repr(transparent)` 目前要求恰好一个字段；额外的零大小字段在 v0.3.x 尚未建模。
- 记录的 ABI 是 target 相关的；Rust 消费方必须按与 provider 相同的 target 与 C ABI 编译。
- `isize` / `usize` 在投影中保留 Rust 拼写；`c_long` / `c_ulong` 映射为 `core::ffi::c_long` / `c_ulong`。其他 primitive 继续使用 ABI 宽度映射。
- panic 与异常不得穿越边界；错误边界契约是受支持的路径。
- 生成文件是普通 Rust，可以在外层再封装成安全的 crate API。
- `amc-rust` 链接 LLVM Support 但不使用 Clang AST；它的前端自行计算布局。
- v0.4.0 起 Rust 语义发现迁移到独立 rustc helper；adapter 契约见 [amc.rust.semantic/1](../architecture/rust-adapter_zh.md)。使用 `-DAMC_BUILD_RUST_HELPER=ON` 可通过 Cargo 构建并安装它。
