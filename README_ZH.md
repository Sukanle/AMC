# AMC

<p align="center">
  中文 · <a href="README.md">English</a>
</p>

AMC 是 ABI 元数据编译器/工具链。这是一个独立的 AMC 代码仓库。

## 文档

- [文档索引](docs/README_ZH.md)
- [AMC 工具链与 CLI](docs/amc/amc_zh.md)
- [`.abic.toml` 配置文件](docs/abic/abic_zh.md)
- [跨语言投影](docs/lang/README_ZH.md)
- [MCP 工具目录](docs/ai/MCP_zh.md)
- [二进制元数据检查](docs/architecture/elf-inspection_zh.md)

## 测试与分析

统一入口为 `python tools/run.py`（同时执行构建与 CTest）。若仅需执行某一步骤，可使用 `tools/run_build.py` 或 `tools/run_test.py`：

```bash
python tools/run.py                         # 构建 + 所有 CTest 组合测试
python tools/run.py --build-only            # 配置并构建
python tools/run.py --elf-pe                # 构建 + ELF/PE 元数据测试
python tools/run.py --bootstrap             # 构建 + 元数据引导测试
python tools/run.py --interop               # 构建 + 互操作性测试
python tools/run.py --mcp --host h.abix --plugin p.abix
```

- `tests/cli/elf_pe_metadata_test.py` 用于验证原生二进制文件中的 ABIX 元数据部分（CTest：`amc-elf-pe-metadata-test`）。
- `tests/cli/bootstrap_test.py` 通过 `amc_add_abi` 验证 AMC 自身的元数据引导机制（CTest：`amc-bootstrap-test`）。
- `tests/cli/mcp_compat_test.py` 通过 JSON-RPC 调用 `amc-mcp` 来判断 ABI 兼容性。
- `scripts/abix_token_cost.py` 用于比较源代码与 `amc context --format llm` 的 Token 消耗情况。

## 独立构建

AMC 更推荐使用已安装的 ABIX 包，无需 ABIX 源代码树。

```bash
cmake -S . -B build \
  -DABIX_DIR=/opt/abix/lib/cmake/ABIX \
  -DCMAKE_PREFIX_PATH="/opt/abix;/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /opt/amc
```

仅会自动获取 ABIX。当 `find_package(ABIX 0.3 CONFIG COMPONENTS runtime)` 失败时，CMake 会将该版本及其子模块克隆到项目内的 `dependencies/` 目录中，并作为仅运行时依赖的子项目进行配置：

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DAMC_ABIX_REF=v0.3.0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

其他所有依赖项——LLVM/Clang、fmt 以及 toml++——均需由用户自行安装；AMC 会直接使用系统中已有的版本，不会下载替代版本。虽然允许使用更高版本，但会强制执行最低版本要求。若这些依赖未位于默认路径，需设置 `CMAKE_PREFIX_PATH`（或相应的 `<package>_DIR` 参数）。

`dependencies/` 目录中存放的是可缓存的、未被跟踪的 ABIX 源码；仅 [`dependencies/README.md`](dependencies/README.md) 被纳入版本控制。在此模式下，AMC 仍可构建并运行测试，但仅安装工具和头文件（不会导出 `AMCConfig.cmake` 包），因为包导出功能需要已安装的 ABIX 包。如需安装该包，需重新配置并设置 `-DABIX_DIR=...` 参数。

**必需的依赖包：**

- ABIX 0.3 运行时及 MICS（若未安装则自动获取）
- LLVM 与 Clang（`clang-cpp`）
- fmt 11.2 及更高版本
- toml++ 3.4 及更高版本
- C++ 测试所需的 Catch2 v3（可选）
- 用于可选 Aue 兼容性测试的 Lua 5.4

AMC 自行实现并维护工具层库（`format`、`abi`、`metadata`、`tools`），并将它们以 `AMC::format`、`AMC::abi`、`AMC::metadata`、`AMC::tools` 的形式导出。这些库是在本地编译的，而非从 ABIX 获取，因为它们的公共头文件及命名空间属于 AMC 的 `SKL/AMC/Core` API。ABIX 运行时仅提供头文件；AMC 作为工具链依赖 LLVM/Clang，并拥有自身的异常处理/RTTI 机制。

独立的 CTest 组合测试涵盖了 C++ 核心/端到端测试、已安装包的消费者测试、功能测试、Rust 提供程序/恐慌处理测试、取消机制测试、互操作性测试、能力生成测试、元数据段验证测试、AMC 核心引导测试，以及独立的 CMake 集成测试。若安装了 Lua 5.4，还会构建 Aue 并运行其 L0/L1 兼容性测试及对应测试用例。随着项目拆分，旧有的仅适用于单模块仓库的 `amc-integration-test` 和 `amc-cmdly-interop-test` 已被废弃。

## 已安装包模式

```cmake
find_package(AMC 0.1 CONFIG REQUIRED)

add_library(my_contract STATIC contract.cpp)
amc_add_abi(
  TARGET my_contract
  CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/contract.abic.toml"
  ABIX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build/contract.abix"
  CPP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/contract_metadata.hpp")
```

`AMC::amc`、各语言提供程序、`AMC::amc-dump`、`AMC::amc-mcp`，以及工具层库 `AMC::format` / `AMC::abi` / `AMC::metadata` / `AMC::tools` 均为可导出目标。`amc_add_abi` 会随包一同安装，并通过导入的 ABIX 目标来定位 ABIX 的包含目录。

AMC 作为独立项目发布，运行时依赖已安装的 ABIX 运行时包。

## 许可证

AMC 采用 [Apache License, Version 2.0](LICENSE) 进行许可。项目相关说明请参见 [`NOTICE`](NOTICE)，而捆绑的依赖项信息则见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。另外，直接引入的 `ljson` 源码仍受 MIT 许可证约束。
