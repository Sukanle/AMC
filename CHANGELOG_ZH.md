# 变更记录

## Unreleased — v0.4.0

### 新增

- Rust Semantic IR 扩展：`.semantic` 新增 `rust` 对象，保存 generics、traits/impls、`Result` errors、lifetimes、trait objects 和 async functions；impl 条目还会记录具体的 associated type 值（如 `type Unit = f64`）。 ABIX 可投影的 types/functions 仍在 ABI 视图中。
- Rust 显式泛型选择：`--instantiate Template=arg1,arg2` / `[[instantiate]]` 按请求输出具体本地实例，不再枚举所有 monomorphization。
- `.abic.toml` 的 `[rust] features` 会转发给 helper；没有 Cargo manifest 时仍记录 CLI features。
- Rust `[[error]]` 契约会自动复用匹配的 helper error enum 作为 error set（名称和非零 variant code），domain、success 和 value slot 仍由显式契约决定。
- `tools/rustc-helper/`：`amc-rustc-helper` 现在是真正的 `rustc_driver` 语义生产者。它输出 `amc.rust.semantic/1` envelope，并提取嵌套 module 中的 `repr(C)` / `repr(transparent)` / packed / align / union / enum 记录、具体泛型实例、public `extern "C"` 函数（含 `impl` 关联函数）、泛型声明、trait/impl 元数据和 `Result` error enum。构建需要 `rustc-dev`。
- `[rust] helper` 配置：`amc-rust` 将 helper 的 types/functions 导入 ABIX/Semantic IR，与源码 parser 交叉校验布局，并写出 semantic sidecar。helper 已证明的语法如果 parser 无法建模，parser 错误会降级为 warning；helper 不可用时仍回退到 parser/probe。
- Cargo metadata 集成：helper 通过 `--manifest-path`（或从 `--source` 向上发现的 manifest）读取 `cargo metadata`，填充 crate name、edition 和声明的 features，并支持 additive `--features`。
- Rust edition 选择：`[rust] edition`（2015/2018/2021/2024）会转发给 helper；未设置时由 Cargo manifest 提供 package edition，否则使用 2021。helper 报告 rustc 实际编译使用的 edition，并在 `cargo.edition` 保留 Cargo package edition。
- Cargo workspace 事实：helper 记录 package id、manifest 路径、workspace root、target directory、workspace/default members 和依赖（kind、requirement、optional、path）。`[rust] manifest` 与 `[rust] target` 会转发给 helper，事实保存在 `.semantic` 的 `rust.cargo`。
- 显式泛型实例现在支持 primitive、非泛型本地类型和 const generic 十进制整数。未知名称、多余参数和非法 const 值会 fail closed，不再生成猜测的实例。
- Rust 容器契约：`[[adapt]]` 将 `[[rust.expose]]` 暴露的 `String`/`&str`、`Vec<T>`/`&[T]`、`Option<T>` 参数映射到 ABIX-STD StringView、SequenceView、Optional 边界类型。AMC 合成边界 record，生成 `#[repr(C)]` 结构和 marshal 代码，并记录 lowering。`Option<T>` 返回值现在作为 value contract 映射到 Optional 边界。`Box<dyn Trait>` 会作为 fat pointer 明确拒绝，不再降级为 thin owned handle。`Vec<T>` / `Option<T>` 适配现在除 primitive 外还支持本地 `#[repr(C)]` record 元素；owned `Vec` marshal 要求 `Clone`。嵌套字段解析不再把父 record 的字段区间与元素类型新增的字段混在一起。
- `Box<T>` 走显式 owned-handle 路径：`[[adapt]] native = "Box" target = "*mut T"` 加上 `[[owned]]` create/destroy 和 `[[contract]]` 所有权模式。机械配对可用 `create = "auto"` / `destroy = "auto"` 自动生成（`Box::new` / `Box::from_raw`），`release = "auto"` 复用匹配的 owned destroy。容器返回值在 owned 契约落地前仍明确拒绝。
- 标准容器事实：`Option<T>`、`Box<T>`、`String`、`Vec<T>` 的出现位置记录在 `.semantic` 的 `rust.containers`。选中进入 ABI 时会要求显式 `[[contract]]`，不会假设 niche 布局或所有权。
- `AMC_BUILD_RUST_HELPER=ON` 会用 Cargo 把 `tools/rustc-helper` 构建到 build 目录（仅在 helper 进程内设置 `RUSTC_BOOTSTRAP=1`），并安装到 AMC 工具旁边；未开启时测试仍按需构建 helper。
- `amc-rust-helper-test`：用 Cargo 构建 helper，验证 generics/traits/errors/cargo metadata，用 rustc `size_of`/`align_of`/`offset_of!` probe 交叉校验布局，并检查 AMC 导入路径。
- `tools/rustc-helper` 改为 Cargo 项目（`Cargo.toml`、`Cargo.lock`、`build.rs`）。命令行交给 `clap`，envelope 交给 `serde_json`，workspace 事实交给 `cargo_metadata`，不再手写参数与 JSON 解析。
- Rust 投影：原始 FFI 声明移入 `raw` 模块；生成的包装保留源码函数名，纯值类型为安全 `pub fn`，指针/owner 契约为 `pub unsafe fn`，所有原始调用统一走生成的 `amc_ffi!` 宏。`Owned*` 和 typed `Result` 包装使用同一宏。
- `tools/imhex/`：为 ABIX v4 `.abix` artifact 和 ABIX Metadata Region 提供 ImHex Pattern Language 定义，覆盖 section directory、全部 record table、extension 契约、字符串名称和嵌入到 native binary 的 `.abix.metadata`。
- `tools/run.py` 默认把 AMC 测试产生的二进制保留到 `build/tmp`（可用 `--artifact-dir` 覆盖），并导出 `AMC_ARTIFACT_DIR`、设置 `TMPDIR`/`TMP`/`TEMP`，使 `.abix`、native library 和测试可执行文件在测试结束后仍可用于检查。

### 破坏性变更

- 采用 ABIX v0.4.0 的显式 ABIX-STD 布局：`abix_optional`、`abix_result`、`abix_str`。`AMC_ABIX_REF` 改为 v0.4.0，并要求 `find_package(ABIX 0.4.0)`。
- 旧 v0.3.x `.abix` artifact 与生成代码不兼容；必须用本树重新生成。

### 变更

- `amc` CLI 和 `amc-cpp` / `amc-rust` / `amc-zig` provider 改用 LLVM `CommandLine` 解析参数。每个命令都是注册的 subcommand，使用类型化选项并自动提供 `--help` / `--version`；手写 argv 循环、手工拆分 `--format=` 和手写 usage 文本已移除。命令拼写通过 MICS `SKL_RFS_ENUM` 统一注册，不带命令运行时会输出 LLVM 的 subcommand 总览。`--error-format` 和每个命令的 `--format` 都是真正的 LLVM 选项，通过 callback 选择错误信封。CLI 采用 git 风格的 `amc <command> [options]` 形式：选项写在命令之后，因此不再需要 argv 重排或引导扫描。每个命令在实现旁边注册 handler，分派改为表查找而不是 switch。
- 语言、provider capability 和 cache action 的分派改用 MICS 注册枚举：`Support/Language.h` 中的 `Language` / `Capability`，以及 `Compilation/Reflect.h` 中的 `CacheAction`。`generate` 的语言链、provider capability 匹配和 `cache stats|prune` 分支改为枚举/表驱动，不再使用字符串比较。
- 函数签名和 record 字段中的 trait object（`&dyn Trait` / `*const dyn Trait`）会被明确拒绝；它是 fat pointer，没有显式契约时 AMC 不再把它降级为 thin pointer。
- Rust semantic 重新加载时也会恢复 async functions 以及其他语言扩展。
- C++ namespace 统一为 `skl::amc`（生成代码为 `skl::amc::generated`），C++ 枚举值使用 `UPPER_SNAKE_CASE`。
- Core/Semantic/IR/Diagnostics/Compilation/Artifact 的枚举名称与解析改由共享 MICS `SKL_RFS_ENUM` 表驱动，不再维护本地 switch。
- `evaluate_optional` / `evaluate_result` / `evaluate_string_view` 支持 v0.4.0 的显式 `reserved` 布局，并拒绝非法的 reserved 字段。
- 合成的 StringView 边界与 C++ thunk 包含显式 reserved 字段；error-boundary 的值字段索引跟随新的 `status, reserved, value` 布局。
- Rust/Zig/C++ interop fixture 改用显式布局。

### 验证

- Linux CTest：35/35 passed。
- `amc-rust-helper-test`：131 项检查，覆盖 Cargo workspace、const/本地类型泛型实例、容器事实、trait object 拒绝和 rustc 布局 probe。
- rustc frontend 工作另行推进；本条只记录 ABIX 锁步。

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
- Windows：当 Zig 不可用时，expose interop 测试会跳过 Zig interface-proxy consumer，与已有的 Zig handle/STD consumer 跳过逻辑一致。

### 兼容性

- ABIX v0.4.0 对 `abix_optional` / `abix_result` / `abix_str` 的破坏性更新不属于本次发布。AMC v0.4.0 将执行锁步迁移并重新生成所有 artifact。

### 已知限制

- cross-target probe 仍为后续工作；Host layout probe 已稳定。
- `packed` / `transparent` / `union` 的 backend emission 延后到 v0.4.0，与一级表示模型一起落地；v0.3.x 只保留布局验证。

### 验证

- Linux：CTest 34/34 passed。
- `amc-boundary-contracts` 与 `amc-boundary-edges --strict`：passed。
- `amc-lease-interop-test`、`amc-rust-expose-test`、`amc-rust-contract-test`、`amc-rust-clong-test`：passed。
- `amc-provider-path-test`：passed。
- Install smoke：`cmake --install` 后通过 PATH 调用 `amc build`。
- Windows/macOS：passed（tag 前已验证）。

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
