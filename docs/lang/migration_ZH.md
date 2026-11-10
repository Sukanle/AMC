# 用户迁移成本预览

<p align="center">
  <a href="migration.md">English</a> · 中文
</p>

本页列出**你需要自行编写**的源码文件，以及 **AMC 会生成**的文件。AMC 编译的是接口表面，而不是整个程序；迁移意味着描述和适配边界，而不是移植实现。

## 1. 自有源码迁移（类似 cmdly）

适用于你拥有 provider 源码和构建系统的场景。provider 保留原有实现，只需暴露 ABI 表面。

### 你需要编写

| 文件                                            | 作用                                                                  | 是否必需                    |
| ----------------------------------------------- | --------------------------------------------------------------------- | --------------------------- |
| Provider 源码（`src/lib.rs`、`*.hpp`、`*.cpp`） | `#[repr(C)]` 记录与 `extern "C"` 函数，或由 `symbols` 选择的 C++ 声明 | 是                          |
| `amc.abic.toml`                                 | `[[import]]` 文件/符号、`[target]`、`[[export]]` 以及可选契约         | 是                          |
| Provider include（`include!("*_thunks.rs")`）   | 把生成的 wrapper 编译进 provider crate                                | 仅使用 `[[rust.expose]]` 时 |
| 消费者源码                                      | 调用生成的投影                                                        | 是                          |
| 构建文件（`Cargo.toml`、`CMakeLists.txt`）      | 构建 provider 与消费者，并运行 `amc build` / `amc generate`           | 是                          |
| 消费者 `build.rs` 或链接参数                    | 定位并链接 provider 原生库                                            | 消费者独立时                |

### AMC 生成

- `.abix`、`.semantic` 与 `.lowering` artifact；
- `amc_generated.rs`、`amc_generated.hpp` 或 `amc_generated.zig`；
- `amc_abi_check.hpp`，或 `const assert!` / `comptime` 布局检查；
- `[[rust.expose]]` 包装普通 Rust 函数时生成 `*_thunks.rs`；
- `OwnerOf*`、`Owned*`、`OwnedString*`、`OwnedSpan*`、`Result` 包装与 `pub mod safe`。

### 最小目录结构

```text
my-project/
├── amc.abic.toml
├── provider/
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs          # 你的实现 + 导出表面
│       └── generated/      # Rust expose 时的 AMC thunks
└── consumer/
    ├── Cargo.toml
    └── src/main.rs         # 使用生成的投影
```

### 成本说明

- 不需要手写 `extern "C"` 声明、owner destroy trampoline 或布局断言。
- 可能需要调整可见性与签名，使导出表面能表示为 ABI。
- Cargo workspace 示例见 [`examples/rust/ffi-workspace/`](../../examples/rust/ffi-workspace/README.md)。

## 2. 已有 DLL 迁移（类似 OpenCV）

适用于原生库已经存在、无法修改其构建或 C++ ABI 的场景。缺的是一层稳定的 C 边界。

### 你需要编写

| 文件                      | 作用                                                                   |
| ------------------------- | ---------------------------------------------------------------------- |
| `*_shim.cpp` / `*_shim.c` | 在现有 API 之上提供 C ABI：opaque handle、生命周期、边界检查与异常捕获 |
| `*_shim_ffi.rs`           | shim 的 Rust `extern "C"` 声明                                         |
| `*_bridge.rs`             | `#[repr(C)]` handle 与普通 Rust 函数；用 `Box<T>` 表达所有权           |
| `amc.abic.toml`           | `[[adapt]]`、`[[rust.expose]]`、`[thunks]`、`[target]` 与 `[[export]]` |
| 消费者源码                | 使用生成的安全 facade                                                  |
| 构建/链接文件             | 编译 shim、链接原库，并链接生成的 provider                             |

### AMC 生成

- owner handle（`abix_owner_<T>`）及其 destroy trampoline；
- `OwnerOf<T>`，带 `as_ref` / `as_mut`、`Drop` 与安全工厂；
- Rust bridge 的 `extern "C"` thunks；
- `amc_generated.*` 与对应语言的布局检查；
- 借用参数 `&T` / `&mut T` 以及 `&str` / `&[T]` 视图的安全 facade。

### 为什么需要 shim

- C++ 类、模板、重载与异常没有稳定的跨语言 ABI。
- OpenCV 5 移除了旧 C API，因此最小的 `cv::Mat` shim 是最简单的桥。
- AMC 投影你暴露的 ABI；它无法仅从头文件推断已有的 C++ 对象模型。

### 最小目录结构

```text
my-bridge/
├── amc.abic.toml
├── native_shim.cpp          # 现有 DLL 之上的 C ABI
├── src/
│   ├── shim_ffi.rs          # extern "C" 声明
│   ├── bridge.rs            # #[repr(C)] handle + Rust 函数
│   └── generated/           # AMC thunks
└── consumer/
    └── src/main.rs          # 安全 facade 消费者
```

### 成本说明

- 一次性成本在 shim 与 handle/所有权映射；消费者保持无 `unsafe`。
- 保持 C ABI 小：create/destroy、少量操作、状态/错误返回。
- 在 shim 内捕获原生异常；不要让异常展开到 Rust。
- 可运行的示例见 [`tests/interop/opencv/`](../../tests/interop/opencv/README.md)。

## AMC 不会要求你编写

- `.abix`、`.semantic` 或 `.lowering` 记录；
- 导出表面的生成声明或手写 `extern` 块；
- `static_assert` / `const assert!` / `comptime` 布局检查；
- 外部内存的 owner destroy trampoline 或 `Drop` 实现；
- 安全 facade。

## 成本概览

| 场景     | 一次性工作                              | 持续工作                 |
| -------- | --------------------------------------- | ------------------------ |
| 自有源码 | 标注导出、编写 `.abic.toml`、接入消费者 | 接口变化后重新生成       |
| 已有 DLL | 编写 C shim 与 Rust bridge、映射所有权  | 原生 API 变化时维护 shim |

从 [Rust 快速开始](rust/quickstart_ZH.md) 或对应语言的[快速开始](cpp/quickstart_ZH.md) / [Zig 快速开始](zig/quickstart_ZH.md)开始。
