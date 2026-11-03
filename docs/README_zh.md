# AMC 文档

<p align="center">
  中文 · <a href="README.md">English</a>
</p>

本目录记录 AMC 工具链：`amc` CLI、`.abic.toml` 配置、语言 provider、MCP 集成与 agent 效率分析。

> ABIX runtime、ABI 模型与 `.abix` 格式文档位于 [ABIX 仓库](https://github.com/Sukanle/ABIX/tree/main/docs)。

## 工具链

| 文档                       | English              | 描述                      |
| -------------------------- | -------------------- | ------------------------- |
| [amc_zh.md](amc/amc_zh.md) | [amc.md](amc/amc.md) | AMC CLI、命令与结构化诊断 |

## 配置

| 文档                          | English                 | 描述                      |
| ----------------------------- | ----------------------- | ------------------------- |
| [abic_zh.md](abic/abic_zh.md) | [abic.md](abic/abic.md) | `.abic.toml` 构建配置参考 |

## 语言

| 文档                              | English                     | 描述                                                |
| --------------------------------- | --------------------------- | --------------------------------------------------- |
| [README_zh.md](lang/README_zh.md) | [README.md](lang/README.md) | ABIX 与 bind 式 FFI 的区别；投影模型                |
| [cpp_zh.md](lang/cpp_zh.md)       | [cpp.md](lang/cpp.md)       | C++ 前端/后端、生成的布局检查、接口层               |
| [rust_zh.md](lang/rust_zh.md)     | [rust.md](lang/rust.md)     | Rust provider、`repr(C)` 投影、所有权/`Result` 包装 |
| [zig_zh.md](lang/zig_zh.md)       | [zig.md](lang/zig.md)       | Zig backend-only 投影、`extern` / `comptime` 检查   |

## AI 与 Agent

| 文档                        | English               | 描述                                       |
| --------------------------- | --------------------- | ------------------------------------------ |
| [MCP_zh.md](ai/MCP_zh.md)   | [MCP.md](ai/MCP.md)   | `amc-mcp` 服务器与 ABI 工具目录            |
| [troi_zh.md](ai/troi_zh.md) | [troi.md](ai/troi.md) | AMC/MCP Agent 工作流的 TROI token 效率度量 |

## 二进制检查

| 文档 | English | 描述 |
| --- | --- | --- |
| [elf-inspection_zh.md](architecture/elf-inspection_zh.md) | [elf-inspection.md](architecture/elf-inspection.md) | 在 ELF / Mach-O 二进制中检查 ABIX 元数据节区 |
| [pe-inspection_zh.md](architecture/pe-inspection_zh.md) | [pe-inspection.md](architecture/pe-inspection.md) | Windows PE 镜像中的 ABIX 私有头与元数据节区 |

## 相关 ABIX 文档

- [ABIX 文档索引](https://github.com/Sukanle/ABIX/blob/main/docs/README_zh.md)
- [ABIX API 参考](https://github.com/Sukanle/ABIX/blob/main/docs/abix/api_zh.md)
- [ABIX ABI 规范](https://github.com/Sukanle/ABIX/blob/main/.agents/ABI-SPEC.md)
- [只依赖 ABIX、不使用 AMC](https://github.com/Sukanle/ABIX/blob/main/docs/abix/without_amc_zh.md)
