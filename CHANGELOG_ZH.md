# 变更记录

## v0.3.1 — 2026-11-07

### 新增

- Rust FFI surface 读取 `[target]` 事实（`arch`、`os`、`abi`、`pointer_bits`、
  `endianness`，可选 `c_long_bits`），并记录到 ABI module。
- `amc-rust-layout-probe-test`：用生成的 rustc `size_of` / `align_of` /
  `offset_of!` probe 对比 AMC 布局。
- Rust parity fixture：pointer-sized primitive、fixed array、type alias、
  非默认 enum repr、packed struct。
- `repr(C, packed)` 布局与 rustc 对比验证。
- void 返回的 `extern "C"` 函数不再被跳过。
- 使用 `rustc --print cfg` 校验 Host target facts。

### 变更

- **Rust import 的破坏性变化：** Rust `.abic.toml` 现在要求 `[target]` 中提供
  `pointer_bits`。旧配置需要补：

  ```toml
  [target]
  arch = 1
  os = 1
  abi = 1
  pointer_bits = 64
  endianness = "little"
  ```

- Rust 的 `pointer`、`isize`、`usize`、`c_long`、`c_ulong` 改为 target-aware；
  移除硬编码 8 字节假设。
- 项目版本、provider 版本统一到 `0.3.1`；MCP server 版本同步对齐。

### 修复

- `#[repr(C, packed)]` 的字段 offset 现在按 packed 计算；之前只降低整体对齐。
- `#[repr(C, packed)]` 布局与 rustc 对比验证。
- 返回 `()` 的 Rust 函数不再被静默丢弃。

### 已知限制

- layout probe 目前只覆盖 Host target；cross-target reflection 延后。
- `repr(C, packed)` 只完成布局验证；backend 的 packed emission 尚未实现，
  不作为完整投影能力宣传。
- `isize` / `c_long` 等 primitive alias 可能与 `i32` / `i64` 共享 ABI type；
  源拼写保留是后续工作。
- Windows x64 需要显式设置 `c_long_bits = 32`。
- trait / associated type / trait object / 复杂 lifetime / Cargo workspace
  仍属于 v0.4.0。

### 验证

- CTest：28/28 passed。
- `boundary --strict`：passed。
- `amc-rust-test`：passed。
- `amc-rust-layout-probe-test`：passed。

## v0.3.0 — 2026-11-06

语义架构发布：semantic model、身份系统、模板、ABI lowering、C++/Rust/Zig
投影、错误契约、接口 proxy、cache 与 v0.3 基准基线。详见 `v0.3.0` tag。
