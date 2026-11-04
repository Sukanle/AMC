# AMC Documentation

<p align="center">
  English · <a href="README_ZH.md">中文</a>
</p>

This directory documents the AMC toolchain: the `amc` CLI, `.abic.toml` configuration, language providers, MCP integration and agent-efficiency analysis.

> ABIX runtime, ABI model and `.abix` format documentation lives in the [ABIX repository](https://github.com/Sukanle/ABIX/tree/main/docs).

## Toolchain

| Document             | 中文                       | Description                                      |
| -------------------- | -------------------------- | ------------------------------------------------ |
| [amc.md](amc/amc.md) | [amc_zh.md](amc/amc_zh.md) | the AMC CLI, commands and structured diagnostics |

## Configuration

| Document                | 中文                          | Description                                |
| ----------------------- | ----------------------------- | ------------------------------------------ |
| [abic.md](abic/abic.md) | [abic_zh.md](abic/abic_zh.md) | `.abic.toml` build configuration reference |

## Languages

| Document                    | 中文                              | Description                                                      |
| --------------------------- | --------------------------------- | ---------------------------------------------------------------- |
| [README.md](lang/README.md) | [README_ZH.md](lang/README_ZH.md) | why ABIX differs from bind-style FFI; the projection model       |
| [cpp.md](lang/cpp.md)       | [cpp_zh.md](lang/cpp_zh.md)       | C++ frontend/backend, generated layout checks, Interface layer   |
| [rust.md](lang/rust.md)     | [rust_zh.md](lang/rust_zh.md)     | Rust provider, `repr(C)` projection, ownership/`Result` wrappers |
| [zig.md](lang/zig.md)       | [zig_zh.md](lang/zig_zh.md)       | Zig backend-only projection, `extern` / `comptime` checks        |

## AI and agents

| Document              | 中文                        | Description                                              |
| --------------------- | --------------------------- | -------------------------------------------------------- |
| [MCP.md](ai/MCP.md)   | [MCP_zh.md](ai/MCP_zh.md)   | the `amc-mcp` server and its ABI tool catalogue          |
| [troi.md](ai/troi.md) | [troi_zh.md](ai/troi_zh.md) | TROI token-efficiency metric for AMC/MCP agent workflows |

## Binary inspection

| Document | 中文 | Description |
| --- | --- | --- |
| [elf-inspection.md](architecture/elf-inspection.md) | [elf-inspection_zh.md](architecture/elf-inspection_zh.md) | inspecting ABIX metadata sections in ELF / Mach-O binaries |
| [pe-inspection.md](architecture/pe-inspection.md) | [pe-inspection_zh.md](architecture/pe-inspection_zh.md) | the ABIX private header and metadata sections in Windows PE images |

## Related ABIX documents

- [ABIX documentation index](https://github.com/Sukanle/ABIX/blob/main/docs/README.md)
- [ABIX API reference](https://github.com/Sukanle/ABIX/blob/main/docs/abix/api.md)
- [ABIX ABI specification](https://github.com/Sukanle/ABIX/blob/main/.agents/ABI-SPEC.md)
- [Using ABIX without AMC](https://github.com/Sukanle/ABIX/blob/main/docs/abix/without_amc.md)
