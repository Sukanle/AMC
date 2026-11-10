# AMC Documentation

<p align="center">
  English · <a href="README_ZH.md">中文</a>
</p>

AMC is an **interface compiler and code-generation toolchain**. It parses source-language interfaces, lowers them into ABIX IR, validates the result, and projects it through language backends.

AMC is not a replacement for the user's compiler or linker. It does not emit machine code, optimize programs, or link binaries. Its output is source-level projections and ABI contracts; the user's toolchain still owns compilation and linking.

```text
Source interfaces
      │
      ▼
Frontend ──────► ABIX IR ──────► Pass ──────► Backend
                    │                            │
                    ├── Compilation ─────────────┤
                    └── Interfaces (CLI/MCP) ────┘
```

## What AMC records

AMC makes the native ABI the contract rather than a C header:

- type identity, layout, field offsets and alignment;
- function signatures, calling conventions and symbols;
- ownership, lifetime and error boundaries;
- template declarations, instances and explicit specialisations;
- interface contracts, versions, slots and capabilities;
- optional semantic sidecars for diagnostics and tooling.

The stable wire formats are `.abix`, `.semantic` and `.lowering`. They contain primitive/ABI records, never LLVM in-memory layouts or pointer values.

## Current status

At the v0.4.0 development boundary the core architecture is in place:

| Area                                              | Status                                                                                                                           |
| ------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Semantic model and identities                     | complete                                                                                                                         |
| Template declarations / instances / `.semantic`   | complete                                                                                                                         |
| ABI lowering and `.lowering`                      | complete                                                                                                                         |
| C++ / Rust / Zig projections                      | complete                                                                                                                         |
| Rust FFI surface                                  | target-aware; probe-validated `repr(C)` / array / alias / enum / packed / transparent / union layout                             |
| Rust adapter                                      | `amc.rust.semantic/1` frozen; explicit `[[rust.expose]]` and ownership/error handoff available; rustc helper available in v0.4.0 |
| Typed `Result`, named error sets, exposed handles | complete                                                                                                                         |
| Interface contracts and proxy layer               | C++ complete; Rust/Zig typed proxies                                                                                             |
| Module boundaries and physical split              | A0–A5 complete; A6 core complete                                                                                                 |
| Performance guardrails                            | pass/artifact/cache/query/multifile suites                                                                                       |
| Multi-file parallelism                            | external `amc` processes, no in-product process pool                                                                             |

### v0.4.0 scope

- The optional Rust helper is covered on Linux: source, build and `-DAMC_BUILD_RUST_HELPER=ON` install. Windows/macOS helper validation and prebuilt distribution are v0.4.x work; the frontend keeps the parser/probe fallback.
- `repr(C, packed)` / union backend emission is v0.4.x/v0.5.0 work. v0.4.0 validates the layout but does not emit packed/union bindings.
- The Cargo workspace fixture is the v0.4.0 real-project boundary. A full real-project fixture plus Cargo profile and extern-dependency facts are v0.4.x work.
- Zig advanced semantics are v0.4.x/v0.5.0 work. 32-bit platforms are unsupported and deferred indefinitely.

See [Benchmarks](bench/README.md) for the current P-core baseline and scaling analysis. Release notes are in [CHANGELOG.md](../CHANGELOG.md).

## Repository layout

```text
AMC/
├── include/SKL/AMC/          public headers
│   ├── Frontend/             language frontends
│   ├── IR/                   IR, identities, artifact loader
│   ├── Pass/                 validation, lowering, compatibility
│   ├── Backend/              C++/Rust/Zig/Lua backends + shared CodeIn/Codegen
│   ├── Compilation/          config, providers, build pipeline, cache
│   ├── Query/                query engine, context rendering
│   ├── Artifact/             metadata region and materialization
│   ├── Diagnostics/          diagnostics model and JSON
│   ├── Platform/             ELF/Mach-O/PE section access
│   ├── Support/              JSON, process helpers
│   └── Private/              internal implementation headers (not installed by default)
├── src/SKL/AMC/              implementation, mirroring include/
├── CodeIn/                   runtime-loaded static templates (C++, Rust, ...)
├── bench/                    Google Benchmark suites
├── tools/                    boundary checks, benchmark runners, stress tools
├── tests/                    unit, conformance, interop, e2e
└── docs/                     this documentation
```

Private implementation headers live under `include/SKL/AMC/Private/` and are only installed with `-DAMC_INSTALL_PRIVATE_HEADERS=ON`. The public include surface stays small; `CodeIn/` assets are loaded at runtime through `amc::backend::read_codein_text`.

## Documentation map

### Getting started

| Document                | 中文                          | Description                          |
| ----------------------- | ----------------------------- | ------------------------------------ |
| [amc.md](amc/amc.md)    | [amc_zh.md](amc/amc_zh.md)    | `amc` CLI, commands, diagnostics     |
| [abic.md](abic/abic.md) | [abic_zh.md](abic/abic_zh.md) | `.abic.toml` configuration reference |

### Architecture

| Document                                                  | 中文                                                            | Description                                                        |
| --------------------------------------------------------- | --------------------------------------------------------------- | ------------------------------------------------------------------ |
| [module-boundaries.md](architecture/module-boundaries.md) | [module-boundaries_zh.md](architecture/module-boundaries_zh.md) | module responsibilities, dependency rules, private layout          |
| [platform-support.md](architecture/platform-support.md)   | [platform-support_zh.md](architecture/platform-support_zh.md)   | 64-bit platform policy and the 32-bit deferral                     |
| [abi-metadata.md](architecture/abi-metadata.md)           | [abi-metadata_zh.md](architecture/abi-metadata_zh.md)           | shared ABI metadata and generated declarations                     |
| [lease-runtime.md](architecture/lease-runtime.md)         | [lease-runtime_zh.md](architecture/lease-runtime_zh.md)         | public lease runtime and generated `LeaseGuard`                    |
| [Benchmarks](bench/README.md)                             | [基准测试](bench/README_ZH.md)                                  | suites, current results, scaling and regression gates              |
| [rust-adapter.md](architecture/rust-adapter.md)           | [rust-adapter_zh.md](architecture/rust-adapter_zh.md)           | `amc.rust.semantic/1` adapter contract for the future rustc helper |

### Languages

| Document                         | 中文                                   | Description                                      |
| -------------------------------- | -------------------------------------- | ------------------------------------------------ |
| [lang/README.md](lang/README.md) | [lang/README_ZH.md](lang/README_ZH.md) | the ABIX projection model                        |
| [cpp.md](lang/cpp.md)            | [cpp_zh.md](lang/cpp_zh.md)            | C++ frontend/backend and compile-time checks     |
| [rust.md](lang/rust.md)          | [rust_zh.md](lang/rust_zh.md)          | Rust provider, `repr(C)`, ownership and `Result` |
| [zig.md](lang/zig.md)            | [zig_zh.md](lang/zig_zh.md)            | Zig backend projection and `comptime` checks     |

### AI and agents

| Document              | 中文                        | Description                                 |
| --------------------- | --------------------------- | ------------------------------------------- |
| [MCP.md](ai/MCP.md)   | [MCP_zh.md](ai/MCP_zh.md)   | `amc-mcp` server and ABI tools              |
| [troi.md](ai/troi.md) | [troi_zh.md](ai/troi_zh.md) | token-efficiency metric for agent workflows |

### Binary inspection

| Document                                            | 中文                                                      | Description                   |
| --------------------------------------------------- | --------------------------------------------------------- | ----------------------------- |
| [elf-inspection.md](architecture/elf-inspection.md) | [elf-inspection_zh.md](architecture/elf-inspection_zh.md) | ABIX metadata in ELF / Mach-O |
| [pe-inspection.md](architecture/pe-inspection.md)   | [pe-inspection_zh.md](architecture/pe-inspection_zh.md)   | ABIX metadata in Windows PE   |

## Build, test and benchmark

```bash
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/Release --parallel
ctest --test-dir build/Release --output-on-failure
python tools/check_module_boundaries.py --strict
```

Benchmarks are optional and use Google Benchmark:

```bash
python tools/run_bench.py --pin p --repetitions 5 --out build/bench
python tools/run_multiprocess_bench.py \
  --artifact build/Release/Aue/generated/build/build/counter.abix \
  --instances 1 2 4 8 16
```

## Design invariants

1. IR does not depend on a source language, Clang AST, or a backend.
2. A backend consumes validated IR only; it never parses source languages.
3. ABI lowering must not mutate the semantic model.
4. `.abix` / `.semantic` / `.lowering` stay stable primitive records.
5. Private implementation headers are not part of the public install surface.
6. Multi-file parallelism belongs to the build system, not to an in-product process pool.
7. Every optimisation keeps CTest and `boundary --strict` green.

## Related ABIX documents

- [ABIX documentation index](https://github.com/Sukanle/ABIX/blob/main/docs/README.md)
- [ABIX API reference](https://github.com/Sukanle/ABIX/blob/main/docs/abix/api.md)
- [ABIX ABI specification](https://github.com/Sukanle/ABIX/blob/main/.agents/ABI-SPEC.md)
- [Using ABIX without AMC](https://github.com/Sukanle/ABIX/blob/main/docs/abix/without_amc.md)
