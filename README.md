# AMC

<p align="center">
  <a href="README_ZH.md">中文</a> · English
</p>

AMC is an interface compiler and code-generation toolchain. This is the standalone AMC repository.

## Documentation

- [Documentation index](docs/README.md)
- [AMC toolchain and CLI](docs/amc/amc.md)
- [`.abic.toml` configuration](docs/abic/abic.md)
- [Cross-language projections](docs/lang/README.md)
- [MCP tool catalogue](docs/ai/MCP.md)
- [Binary metadata inspection](docs/architecture/elf-inspection.md)
- [Benchmarks and performance analysis](docs/bench/README.md)
- [Changelog](CHANGELOG.md)

## Platform support

AMC and ABIX target 64-bit platforms only. 32-bit and 16-bit targets are unsupported and deferred indefinitely; they are not on the v0.4.x/v0.5.0 roadmap. See [platform support](docs/architecture/platform-support.md) for the identity-hash, metadata/query-table and ABI reasons.

## Tests and analysis

The unified entry point is `python tools/run.py` (build + CTest). Use `tools/run_build.py` or `tools/run_test.py` when only one stage is needed:

```bash
python tools/run.py                         # build + all CTest suites
python tools/run.py --build-only            # configure and build
python tools/run.py --elf-pe                # build + ELF/PE metadata test
python tools/run.py --bootstrap             # build + metadata bootstrap test
python tools/run.py --interop               # build + interop tests
python tools/run.py --mcp --host h.abix --plugin p.abix
```

- `tests/cli/elf_pe_metadata_test.py` verifies the ABIX metadata section in a native binary (CTest: `amc-elf-pe-metadata-test`).
- `tests/cli/bootstrap_test.py` verifies AMC's own metadata bootstrap through `amc_add_abi` (CTest: `amc-bootstrap-test`).
- `tests/cli/mcp_compat_test.py` drives `amc-mcp` over JSON-RPC for an ABI compatibility verdict.
- `scripts/abix_token_cost.py` compares the source and `amc context --format llm` token costs.

## Standalone build

AMC prefers an installed ABIX package. It does not need the ABIX source tree.

```bash
cmake -S . -B build \
  -DABIX_DIR=/opt/abix/lib/cmake/ABIX \
  -DEPCD_DIR=/opt/epcd/lib/cmake/EPCD \
  -DCMAKE_PREFIX_PATH="/opt/abix;/opt/epcd;/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /opt/amc
```

ABIX and EPCD are fetched automatically. When `find_package(ABIX 0.4.0 ...)` or `find_package(EPCD 0.1 ...)` fails, CMake clones the missing dependency into the repository-local `dependencies/` directory and configures it as a subproject:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DAMC_ABIX_REF=v0.4.0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

All other dependencies — LLVM/Clang, fmt and toml++ — are expected to be installed by the user; AMC uses the versions already available on the system and never downloads replacements. Minimum supported versions are enforced, so newer releases (including newer major versions) are accepted as-is. Set `CMAKE_PREFIX_PATH` (or the corresponding `<package>_DIR` value) when they are not in a default location.

`dependencies/` is a cacheable, untracked ABIX/EPCD checkout; only [`dependencies/README.md`](dependencies/README.md) is tracked. In this mode AMC still builds and runs its tests, but installs only the tools and headers (not the `AMCConfig.cmake` package export), because the package export requires installed ABIX and EPCD packages. Reconfigure with `-DABIX_DIR=... -DEPCD_DIR=...` to install the package.

Required packages:

- `ABIX 0.4.0` runtime and MICS (fetched automatically when not installed)
- EPCD 0.1 Core/Interface (fetched automatically when not installed)
- LLVM and Clang (`clang-cpp`); providers link shared `libLLVM` when available (pass `-DAMC_LLVM_DYNAMIC=OFF` for the static fallback)
- fmt 11.2 or newer
- toml++ 3.4 or newer
- Catch2 v3 for the C++ tests (optional)
- Lua 5.4 for the optional Aue conformance prototype

AMC owns the artifact-layer libraries (`format`, `abi`, `metadata`, `tools`) and exports them as `AMC::format`, `AMC::abi`, `AMC::metadata` and `AMC::tools`. They are compiled here rather than borrowed from ABIX, because their public headers and namespace are AMC's `SKL/AMC/Core` API. ABIX runtime remains header-only; AMC is the toolchain that needs LLVM/Clang and keeps its own exception/RTTI policy.

The standalone CTest suite covers the C++ core/e2e tests, the installed package consumer, feature tests, Rust provider/panic tests, cancellation, interop, capability generation, metadata section verification, the AMC core bootstrap and an independent CMake integration test. When Lua 5.4 is available, `Aue` also builds the L0/L1 conformance runner and its tests. The old monorepo-only `amc-integration-test` and `amc-cmdly-interop-test` were retired with the split.

## Installed package

```cmake
find_package(AMC 0.4 CONFIG REQUIRED)

add_library(my_contract STATIC contract.cpp)
amc_add_abi(
  TARGET my_contract
  CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/contract.abic.toml"
  ABIX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build/contract.abix"
  CPP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/contract_metadata.hpp")
```

`AMC::amc`, the language providers, `AMC::amc-dump`, `AMC::amc-mcp` and the artifact libraries `AMC::format` / `AMC::abi` / `AMC::metadata` / `AMC::tools` are exported targets. `amc_add_abi` is installed with the package and resolves the ABIX include directories through the imported ABIX targets.

AMC is released independently and consumes an installed ABIX runtime package.

## License

AMC is licensed under the [Apache License, Version 2.0](LICENSE). See [`NOTICE`](NOTICE) for the project notice and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for bundled dependencies. The vendored `ljson` sources remain under the MIT license.
