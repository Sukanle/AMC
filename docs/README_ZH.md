# AMC 文档

<p align="center">
  中文 · <a href="README.md">English</a>
</p>

AMC 是**接口编译器与代码生成工具链**。它解析源语言接口，降低为 ABIX IR，验证结果，再通过语言后端投影为目标代码。

AMC 不替代用户的编译器或链接器：它不生成机器码、不做程序优化、不执行链接。 AMC 的输出是源码级投影和 ABI 契约；实际编译与链接仍由用户工具链完成。

```text
源语言接口
      │
      ▼
Frontend ──────► ABIX IR ──────► Pass ──────► Backend
                    │                            │
                    ├── Compilation ─────────────┤
                    └── Interfaces (CLI/MCP) ────┘
```

## AMC 记录什么

AMC 把原生 ABI 作为契约，而不是让 C 头文件成为唯一事实来源：

- 类型身份、布局、字段偏移与对齐；
- 函数签名、调用约定与符号；
- 所有权、生命周期与错误边界；
- 模板声明、实例化与显式特化；
- 接口契约、版本、slot 与能力；
- 可选的 semantic sidecar，用于诊断和工具链。

稳定 wire format 是 `.abix`、`.semantic` 和 `.lowering`。它们只保存 primitive/ABI record，不写入 LLVM 内存布局或指针值。

## 当前状态

v0.4.0 已发布，核心架构已经就位（v0.4.1 新增 Rust 安全 facade 与端到端 Cargo workspace 示例）：

| 领域                                       | 状态                                                                                                            |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------- |
| Semantic model 与身份系统                  | 完成                                                                                                            |
| 模板声明 / 实例化 / `.semantic`            | 完成                                                                                                            |
| ABI lowering 与 `.lowering`                | 完成                                                                                                            |
| C++ / Rust / Zig 投影                      | 完成                                                                                                            |
| Rust FFI surface                           | target-aware；probe 验证 `repr(C)` / array / alias / enum / packed / transparent / union 布局                   |
| Rust adapter                               | `amc.rust.semantic/1` 已冻结；显式 `[[rust.expose]]` 与 ownership/error 交接可用；rustc helper 已在 v0.4.0 提供 |
| typed `Result`、命名错误集、exposed handle | 完成                                                                                                            |
| 接口契约与 proxy 层                        | C++ 完成；Rust/Zig typed proxy                                                                                  |
| 模块边界与物理拆分                         | A0–A5 完成；A6 核心完成                                                                                         |
| 性能护栏                                   | pass/artifact/cache/query/multifile 套件                                                                        |
| 多文件并行                                 | 由外部多个 `amc` 进程完成，产品内无进程池                                                                       |

### v0.4.0 范围

- 可选的 Rust helper 在 v0.4.0 中覆盖 Linux：源码、构建和 `-DAMC_BUILD_RUST_HELPER=ON` 安装。Windows/macOS 的 helper 验证与预编译分发属于 v0.4.x；前端保留 parser/probe fallback。
- `repr(C, packed)` / union 的 backend emission 属于 v0.4.x/v0.5.0。v0.4.0 只验证布局，不生成 packed/union 绑定。
- v0.4.0 的真实项目边界是 Cargo workspace fixture。完整的真实项目、Cargo profile 与 extern dependency 事实属于 v0.4.x。
- Zig 高级语义属于 v0.4.x/v0.5.0。32 位平台不支持，无限期延后。

当前 P-core 基线与扩展性分析见[基准测试](bench/README_ZH.md)。发布说明见[变更记录](../CHANGELOG_ZH.md)。

## 仓库结构

```text
AMC/
├── include/SKL/AMC/          公共头文件
│   ├── Frontend/             语言前端
│   ├── IR/                   IR、身份系统、artifact loader
│   ├── Pass/                 验证、lowering、兼容性
│   ├── Backend/              C++/Rust/Zig/Lua 后端 + 共享 CodeIn/Codegen
│   ├── Compilation/          配置、provider、构建流水线、缓存
│   ├── Query/                查询引擎、context 渲染
│   ├── Artifact/             metadata region 与 materialize
│   ├── Diagnostics/          诊断模型与 JSON
│   ├── Platform/             ELF/Mach-O/PE section 访问
│   ├── Support/              JSON、进程辅助
│   └── Private/              内部实现头文件（默认不安装）
├── src/SKL/AMC/              实现，目录与 include 对应
├── CodeIn/                   运行时加载的静态模板（C++、Rust 等）
├── bench/                    Google Benchmark 套件
├── tools/                    边界检查、基准运行器、压力测试
├── tests/                    unit、conformance、interop、e2e
└── docs/                     本文档
```

私有实现头文件位于 `include/SKL/AMC/Private/`，只有显式开启 `-DAMC_INSTALL_PRIVATE_HEADERS=ON` 才会安装。公共 include surface 保持收敛；`CodeIn/` 资源通过 `amc::backend::read_codein_text` 在运行时加载。

## 文档地图

### 入门

| 文档                          | English                 | 描述                  |
| ----------------------------- | ----------------------- | --------------------- |
| [amc_zh.md](amc/amc_zh.md)    | [amc.md](amc/amc.md)    | `amc` CLI、命令与诊断 |
| [abic_zh.md](abic/abic_zh.md) | [abic.md](abic/abic.md) | `.abic.toml` 配置参考 |

### 架构

| 文档                                                            | English                                                   | 描述                                                    |
| --------------------------------------------------------------- | --------------------------------------------------------- | ------------------------------------------------------- |
| [module-boundaries_zh.md](architecture/module-boundaries_zh.md) | [module-boundaries.md](architecture/module-boundaries.md) | 模块职责、依赖规则与私有实现布局                        |
| [platform-support_zh.md](architecture/platform-support_zh.md)   | [platform-support.md](architecture/platform-support.md)   | 64 位平台策略与 32 位无限期延后                         |
| [abi-metadata_zh.md](architecture/abi-metadata_zh.md)           | [abi-metadata.md](architecture/abi-metadata.md)           | ABIX ABI 元数据与共享声明                               |
| [lease-runtime_zh.md](architecture/lease-runtime_zh.md)         | [lease-runtime.md](architecture/lease-runtime.md)         | 公共 lease 运行时与生成的 `LeaseGuard`                  |
| [基准测试](bench/README_ZH.md)                                  | [Benchmarks](bench/README.md)                             | 套件、当前结果、扩展性与回归门槛                        |
| [rust-adapter_zh.md](architecture/rust-adapter_zh.md)           | [rust-adapter.md](architecture/rust-adapter.md)           | 未来 rustc helper 的 `amc.rust.semantic/1` adapter 契约 |

### 语言

| 文档                                         | English                                | 描述                                        |
| -------------------------------------------- | -------------------------------------- | ------------------------------------------- |
| [lang/README_ZH.md](lang/README_ZH.md)       | [lang/README.md](lang/README.md)       | ABIX 投影模型                               |
| [lang/migration_ZH.md](lang/migration_ZH.md) | [lang/migration.md](lang/migration.md) | 迁移成本：自有源码或 DLL 需要编写什么       |
| [C++ 中文](lang/cpp/README_ZH.md)            | [C++](lang/cpp/README.md)              | C++ 前端/后端与编译期检查                   |
| [Rust 中文](lang/rust/README_ZH.md)          | [Rust](lang/rust/README.md)            | Rust provider、`repr(C)`、所有权与 `Result` |
| [Zig 中文](lang/zig/README_ZH.md)            | [Zig](lang/zig/README.md)              | Zig 后端投影与 `comptime` 检查              |

### AI 与 Agent

| 文档                        | English               | 描述                          |
| --------------------------- | --------------------- | ----------------------------- |
| [MCP_zh.md](ai/MCP_zh.md)   | [MCP.md](ai/MCP.md)   | `amc-mcp` 服务器与 ABI 工具   |
| [troi_zh.md](ai/troi_zh.md) | [troi.md](ai/troi.md) | Agent 工作流的 token 效率度量 |

### 二进制检查

| 文档                                                      | English                                             | 描述                          |
| --------------------------------------------------------- | --------------------------------------------------- | ----------------------------- |
| [elf-inspection_zh.md](architecture/elf-inspection_zh.md) | [elf-inspection.md](architecture/elf-inspection.md) | ELF / Mach-O 中的 ABIX 元数据 |
| [pe-inspection_zh.md](architecture/pe-inspection_zh.md)   | [pe-inspection.md](architecture/pe-inspection.md)   | Windows PE 中的 ABIX 元数据   |

## 构建、测试与基准

```bash
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/Release --parallel
ctest --test-dir build/Release --output-on-failure
python tools/check_module_boundaries.py --strict
```

基准测试可选，使用 Google Benchmark：

```bash
python tools/run_bench.py --pin p --repetitions 5 --out build/bench
python tools/run_multiprocess_bench.py \
  --artifact build/Release/Aue/generated/build/build/counter.abix \
  --instances 1 2 4 8 16
```

## 设计约束

1. IR 不依赖具体源语言、Clang AST 或后端。
2. 后端只消费已验证 IR，不解析源语言。
3. ABI lowering 不得修改 semantic model。
4. `.abix` / `.semantic` / `.lowering` 保持稳定 primitive record。
5. 私有实现头文件不属于公共安装面。
6. 多文件并行由构建系统负责，产品内不实现进程池。
7. 每次优化都必须保持 CTest 与 `boundary --strict` 通过。

## 相关 ABIX 文档

- [ABIX 文档索引](https://github.com/Sukanle/ABIX/blob/main/docs/README_ZH.md)
- [ABIX API 参考](https://github.com/Sukanle/ABIX/blob/main/docs/abix/api_zh.md)
- [ABIX ABI 规范](https://github.com/Sukanle/ABIX/blob/main/.agents/ABI-SPEC.md)
- [只依赖 ABIX、不使用 AMC](https://github.com/Sukanle/ABIX/blob/main/docs/abix/without_amc_zh.md)
