# Rust

<p align="center">
  <a href="README.md">English</a> · 中文
</p>

Rust 是 ABIX 的第一个非 C++ provider。`amc-rust` 包含源码级前端与 Rust 后端；它链接 LLVM Support 处理文件系统与文件 I/O，但不使用 Clang AST。

## 你会得到什么

- `amc_generated.rs`：`#[repr(C)]` 记录、`extern "C"` 入口与 `const assert!` 布局检查。
- `Owned*`、`OwnerOf*`、`OwnedString*`、`OwnedSpan*` RAII 包装，通过记录的入口释放。
- `pub mod safe` 安全 facade；消费者 crate 可启用 `#![deny(unsafe_code)]`。
- 错误边界对应的带类型 `Result` 包装。

## 角色

- `amc --describe-language rust` 报告 `frontend backend protocol=jsonl-v1`。
- 前端从 `.rs` 文件中读取 `#[repr(C)]` struct/enum 与 `extern "C"` 函数，并自行计算 C 布局。
- 后端从已有的 `.abix` artifact 生成 `amc_generated.rs`。
- `amc-rust` 是独立的 provider 进程；它从不修改 ABIX IR。

## Owner handle 与安全借用

生成的投影会校验 owner handle，并把源码中的 `&T` / `&mut T` 参数暴露为 Rust 借用：

```rust
#![deny(unsafe_code)]
use skl::abix::safe::*;

let owner = box_widget_make(7)?;
assert_eq!(box_widget_value(owner), 7);
```

裸指针、`from_raw` / `into_raw` 和 `LeaseGuard::acquire` 仍作为显式逃生口保留。lease host、容器契约与完整生成表面见[高级特性](advanced_ZH.md)。

## 下一步

- [快速开始](quickstart_ZH.md)
- [高级特性：前端、契约与安全包装](advanced_ZH.md)
- [真实 Cargo workspace 示例](../../../examples/rust/ffi-workspace/README.md)
