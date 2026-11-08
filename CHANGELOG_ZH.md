# 变更记录

## v0.3.3 — 2026-11-09

### 新增

- ABIX 跨语言 lease C ABI 的 AMC Host 实现：`abix_lease_registry`、`abix_lease_api` function table、module instance 同步查找、CLOSING/BUSY 状态和 draining 校验。
- `tests/interop/lease/`：C++ 契约测试以及 Rust/Zig owned-object wrapper，覆盖 acquire -> create -> destroy -> release、draining、重复 release 和 registry destroy。
- Zig -> Rust 接管 fixture：Zig 创建 object + lease，Rust cdylib 接管并执行 destroy -> release，Zig 完成最终 unregister。
- `amc-lease-interop-test`：使用同一 ABIX checkout 构建 provider、Host bridge 和 C++/Rust/Zig 消费者。
- 显式 `[[rust.expose]]` 交接：AMC 记录导出符号，并为普通 Rust 函数生成 `#[export_name = "<symbol>"] pub extern "C"` wrapper；`amc-rust-expose-test` 编译并调用生成的 wrapper。
- Rust ownership / error contract 交接：`[[owned]]`、`[[contract]]`、`[[error]]` 在 Rust 侧端到端验证；`amc-rust-contract-test` 编译 native provider，并检查 RAII 与 `Result` wrapper。

### 变更

- 对齐 ABIX v0.3.4（`AMC_ABIX_REF=v0.3.4`），这是 additive v0.3.3 surface 之上的 docs-only 锁步修正；不改变 STD 布局。
- `[export] symbols` 不再把普通 Rust 函数记录为 C ABI；需要显式使用 `[[rust.expose]]` 生成 wrapper。
- 修复 Rust 指针或数组拼写在多个签名中重复出现时产生的重复 TypeID。
- Rust 投影保留 `isize` / `usize`，并将 `c_long` / `c_ulong` 映射为 `core::ffi::c_long` / `c_ulong`，不再统一折叠为 `c_longlong`。
- Windows x64（`os=1`）现在默认 `c_long` / `c_ulong` 为 32 位；显式 `c_long_bits` 仍然优先。

### 修复

- `amc` 通过 PATH 调用（`argv[0]` 为裸名字）时，现在会从真实可执行文件所在目录查找 `amc-<language>`，而不是当前目录。安装后的工具不再报 "provider closed the response stream"。

### 兼容性

- ABIX v0.4.0 对 `abix_optional` / `abix_result` / `abix_str` 的破坏性更新不属于本次发布。AMC v0.4.0 将执行锁步迁移并重新生成所有 artifact。

### 已知限制

- cross-target probe 仍为后续工作；Host layout probe 已稳定。
- `packed` / `transparent` / `union` 的 backend emission 延后到 v0.4.0，与一级表示模型一起落地；v0.3.x 只保留布局验证。
- Windows 和 macOS 验证是本次发布的最终 tag 前门槛。

### 验证

- Linux：CTest 34/34 passed。
- `amc-boundary-contracts` 与 `amc-boundary-edges --strict`：passed。
- `amc-lease-interop-test`、`amc-rust-expose-test`、`amc-rust-contract-test`、`amc-rust-clong-test`：passed。
- `amc-provider-path-test`：passed。
- Install smoke：`cmake --install` 后通过 PATH 调用 `amc build`。
- Windows/macOS：在创建 v0.3.3 tag 前验证。

## v0.3.2 — 2026-11-07

### 新增

- 冻结 `amc.rust.semantic/1` adapter 契约，供未来 rustc helper 使用；覆盖 target、crate、types、fields、functions 和参数 ownership。
- `amc-rust-semantic-schema-test`：验证 sample document、未知可选字段和 required 字段失败。
- Rust 布局支持 `#[repr(transparent)]`、`#[repr(C, packed(N))]` 和 `#[repr(C)]` union。
- rustc probe 覆盖 transparent、packed(N) 和 union 布局。

### 变更

- v0.3.x 的 Rust 工作明确为 v0.4.0 rustc frontend 的过渡适配；完整 trait/generic/macro/Cargo 语义延后。
- Rust parser 对 `repr(transparent)` 做单字段布局校验。

### 已知限制

- advanced repr 只完成布局验证；C++/Rust/Zig backend 的 packed/union emission 尚未完整实现。
- `repr(transparent)` 尚未建模额外的零大小字段。
- cross-target probe、显式 `[[rust.expose]]` wrapper 交接和 error/ownership contract 交接仍属于后续 v0.3.x。

### 验证

- CTest：29/29 passed。
- `boundary --strict`：passed。
- `amc-rust-layout-probe-test`：passed。
- `amc-rust-semantic-schema-test`：passed。

## v0.3.1 — 2026-11-07

### 新增

- Rust FFI surface 读取 `[target]` 事实（`arch`、`os`、`abi`、`pointer_bits`、`endianness`，可选 `c_long_bits`），并记录到 ABI module。
- `amc-rust-layout-probe-test`：用生成的 rustc `size_of` / `align_of` / `offset_of!` probe 对比 AMC 布局。
- Rust parity fixture：pointer-sized primitive、fixed array、type alias、非默认 enum repr、packed struct。
- `repr(C, packed)` 布局与 rustc 对比验证。
- void 返回的 `extern "C"` 函数不再被跳过。
- 使用 `rustc --print cfg` 校验 Host target facts。

### 变更

- **Rust import 的破坏性变化：** Rust `.abic.toml` 现在要求 `[target]` 中提供 `pointer_bits`。旧配置需要补：

  ```toml
  [target]
  arch = 1
  os = 1
  abi = 1
  pointer_bits = 64
  endianness = "little"
  ```

- Rust 的 `pointer`、`isize`、`usize`、`c_long`、`c_ulong` 改为 target-aware；移除硬编码 8 字节假设。
- 项目版本、provider 版本统一到 `0.3.1`；MCP server 版本同步对齐。

### 修复

- `#[repr(C, packed)]` 的字段 offset 现在按 packed 计算；之前只降低整体对齐。
- `#[repr(C, packed)]` 布局与 rustc 对比验证。
- 返回 `()` 的 Rust 函数不再被静默丢弃。

### 已知限制

- layout probe 目前只覆盖 Host target；cross-target reflection 延后。
- `repr(C, packed)` 只完成布局验证；backend 的 packed emission 尚未实现，不作为完整投影能力宣传。
- `isize` / `c_long` 等 primitive alias 可能与 `i32` / `i64` 共享 ABI type；源拼写保留是后续工作。
- Windows x64 需要显式设置 `c_long_bits = 32`。
- trait / associated type / trait object / 复杂 lifetime / Cargo workspace 仍属于 v0.4.0。

### 验证

- CTest：28/28 passed。
- `boundary --strict`：passed。
- `amc-rust-test`：passed。
- `amc-rust-layout-probe-test`：passed。

## v0.3.0 — 2026-11-06

语义架构发布：semantic model、身份系统、模板、ABI lowering、C++/Rust/Zig 投影、错误契约、接口 proxy、cache 与 v0.3 基准基线。详见 `v0.3.0` tag。
