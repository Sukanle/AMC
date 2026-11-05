# AMC 模块边界（A0）

状态：**A0 边界冻结**。本文只定义 Frontend、IR、Pass、Backend、Compilation 和 Interfaces 之间的契约，不搬迁现有代码；A1 再开始物理拆分。

## 目标

- 以 ABIX IR 为中心，而不是围绕某一种源语言组织功能。
- 明确并逐步机械化检查模块依赖方向。
- 迁移过程中保持现有 interop/conformance 测试通过。

## 模块

| 模块 | 职责 | 拥有 |
| --- | --- | --- |
| Frontend | 理解源语言并输出 IR | 语言解析、AST 提取、源码语义 |
| IR | 表达与语言无关的接口语义 | 类型、身份、ABI 映射、所有权、生命周期、错误、版本、能力 |
| Pass | 验证和转换 IR | 规范化、验证、兼容性、lowering |
| Backend | 从已验证 IR 生成目标文件 | C++/Rust/Zig/Protobuf/gRPC 生成器 |
| Compilation | 编排一次接口编译 | Context、Session、Pipeline、Cache、构建选项 |
| Interfaces | 对外暴露编译能力 | CLI、MCP、LSP、SDK |

支撑模块：`Artifact`、`Diagnostics`、`Platform`、`Serialization`、`Query`、`Support`。

## 依赖规则

允许：

```text
Frontend ──► IR
Pass     ──► IR + Diagnostics
Backend  ──► IR + Diagnostics
Compilation ──► Frontend + Pass + Backend + IR
Interfaces  ──► Compilation
```

禁止：

- IR 不得包含源语言解析器、Clang AST 或 Backend。
- Frontend 不得包含 Backend，也不得直接调用 Backend。
- Backend 不得包含 Clang AST，也不得解析源码。
- CLI/MCP/LSP/SDK 不得自行实现 configure/frontend/pass/backend 流程。
- Pass 不得依赖特定 Backend。
- 任何模块不得绕过 Diagnostics 直接吞掉可恢复错误。

## 数据流与所有权

```text
Request
  │
  ▼
Frontend ──► IR（由 Compilation 拥有）
  │
  ▼
Pass Pipeline（修改工作副本）
  │
  ▼
Backend（借用已验证 IR，生成 artifacts）
  │
  ▼
Result + Diagnostics
```

- Compilation Session 拥有 IR 和所有工作副本。
- Pass 可以修改 IR，但返回 `true` 时必须保持 IR 有效。
- Backend 接收 `const IR*`，不得修改。
- Diagnostics 是跨模块唯一的可恢复错误通道。
- C++ 异常不得跨越模块边界。

## A0 契约头文件

| 头文件 | 契约 |
| --- | --- |
| `include/SKL/AMC/IR/Module.h` | `amc::ir::Module`、schema version |
| `include/SKL/AMC/Diagnostics/Diagnostics.h` | `Diagnostic`、`Severity`、`DiagnosticList` |
| `include/SKL/AMC/Frontend/Frontend.h` | `Request`、`Result`、`Frontend` |
| `include/SKL/AMC/Pass/Pass.h` | `Context`、`Pass`、`Pipeline` |
| `include/SKL/AMC/Backend/Backend.h` | `Artifact`、`Request`、`Result`、`Backend` |
| `include/SKL/AMC/Compilation/Compilation.h` | `Request`、`Result`、`Compiler` |

A0 的 `ir::Module` 暂时聚合现有 ABI 模块和 Semantic Model，等 A2 再合并数据布局。

## IR 职责

IR 必须区分：

- 逻辑类型（例如扩展浮点）；
- 物理表示（IEEE binary64、x87 extended、PPC double-double）；
- ABI 映射（size、alignment、register class、calling convention）。

IR 还承载 Type/Interface/Method Identity、所有权、生命周期、错误契约、版本、能力和元数据。IR schema version 与运行时 ABI version 独立。

## A4 进度

- `ExportSpec`、`parse_exports`、`apply_target_and_layout` 已迁入 `Compilation/Config`
- Provider manifest/IPC/dispatch/generate 已迁入 `Compilation/Provider`
- build pipeline 已迁入 `Compilation/Build`；`.abix.meta` 输出迁入 `Compilation/Meta`
- CLI 共享格式化/诊断 helper 迁入 `CLI/Support`；CLI.h 不再把 IR 拖进接口层
- 未登记禁止边为 0，allowlist 为空

## 兼容转发头

A1 在 provider 迁移期间使用过显式标记的转发头。现在所有 provider 都直接包含目标头文件，**已无兼容转发头**。

未来如再引入转发头，必须带 `AMC_COMPAT_FORWARD` 标记、写明目标头文件，并在 `tools/check_module_boundaries.py` 报告没有 include 用户后删除。

使用：

```bash
python3 tools/check_module_boundaries.py            # report-only
python3 tools/check_module_boundaries.py --strict   # 未登记禁止边失败
python3 tools/check_module_boundaries.py --contracts-only
```

临时例外记录在 `tools/module_boundary_allowlist.txt`，每行格式为
`file|from|to|until|reason`。`until` 指定移除该边的阶段；过期条目会在报告中提示，避免 allowlist 腐化。`--strict` 接受已登记例外，只对未登记的禁止边失败。

当前例外：无。allowlist 文件刻意保持为空。

CTest 同时运行 `--contracts-only` 和 `--strict`：
`amc-boundary-contracts`、`amc-boundary-edges`。

## M4 进度

- `IR/Lowering.h` 定义 `AbiTypeId` / `AbiFunctionId`、lowered type/function/
  ownership/template 记录和 `LoweringResult` 契约
- `Pass/Lowering` 把语义类型、函数、继承、字段、所有权和已实例化模板链接到 ABI 实体
- `ir::Module` 携带 lowering 结果；`amc-lowering-test` 覆盖映射、直接
  `abi_type` / `abi_signature` 链接、缺失/歧义实体、strict 模式和 lowering JSON 往返
- C++ frontend 把每个 ABI type/function 同步进语义模型并写入直接链接；
  `Semantic/Sync` 可供其他 frontend 复用
- `Semantic/Serialize` 支持读写 sidecar，包含 records、functions、ownership 和 ABI 链接
- `Compilation/Build` 读取 sidecar、补齐缺失链接、运行 `pass::lower`，在符号投影后写出
  `<artifact>.lowering`
- Backend 加载可选的 lowering sidecar，消费 lowered type/function 索引和所有权契约，
  不再按名称重新推导

## 当前 → 目标映射

| 当前 | 目标 |
| --- | --- |
| `Core/Core.h`（`AbiModule`） | `IR/` ABI view |
| `Semantic/*` | `IR/` semantic view |
| `Lang/C++/Frontend.cpp` | `Frontend/Cpp/` |
| `Lang/C++/Backend.cpp` | `Backend/Cpp/` |
| `Lang/Rust/*` | `Frontend/Rust/` + `Backend/Rust/` |
| `Lang/Zig/*` | `Backend/Zig/` |
| `CLI/*` | `Interfaces/CLI/` + `Compilation/` |
| `Tool/MCP/*` | `Interfaces/MCP/` |
| `Core/Verify.cpp` | `Pass/Compatibility` |
| `Core/Cache.cpp` | `Compilation/Cache` |
| `Core/ELF.cpp` | `Platform/ELF` |
| `Core/Metadata.cpp` | `Artifact/` / `Serialization/` |
| `Core/Query.cpp`、`Core/Context.cpp` | `Query/` |
| `Tool/Dump/*` | `Interfaces/CLI` 或 `Query/` |

## A1 进度

- `Lang/C++/Frontend.cpp` → `Frontend/Cpp/Frontend.cpp`
- `Lang/C++/Backend.cpp` → `Backend/Cpp/Backend.cpp`
- `Lang/Rust/Frontend.cpp` → `Frontend/Rust/Frontend.cpp`
- `Lang/Rust/Backend.cpp` → `Backend/Rust/Backend.cpp`
- `Lang/Zig/Backend.cpp` → `Backend/Zig/Backend.cpp`
- 所有 provider 已直接包含目标头文件，兼容转发头已删除
- 扫描当前未列出任何未登记禁止边；只剩 3 条 CLI -> IR 的 A4 例外
- `amc-boundary-contracts` 与 `amc-boundary-edges` 已加入 CTest

## 迁移规则

1. 同一步不同时搬目录和改行为。
2. 先冻结契约头文件，再迁移第一个实现。
3. 迁移期间保留兼容转发头文件。
4. 每一步都必须保持 `ctest` 和 interop 测试通过。
5. 模块只能新增允许方向上的依赖。
6. 新代码必须使用 A0 契约头文件，不能跨模块直接依赖实现。

## A0 验收

- [x] 六个模块职责和支撑模块已文档化。
- [x] 依赖方向和禁止边已文档化。
- [x] 数据流、所有权和诊断规则已文档化。
- [x] 边界头文件存在并可编译。
- [x] `amc-boundary-test` 覆盖契约类型和 Pass 顺序。
- [x] 已有 report-only 依赖扫描和 A0 契约头 gate。
- [x] `--strict` 边 gate 通过，并保留有记录的 A4 例外。
- [ ] A1 将现有 include 迁移到契约头文件后面。

## A1 入口条件

- 契约头文件被接受为模块公共表面。
- Frontend/Backend 拆分有文件级迁移映射。
- 依赖检查能以 report-only 模式扫描当前代码树（已完成）。
- 任何 A0 契约头文件都不包含 Clang AST、Backend 或 Interface 层。
