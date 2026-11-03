# AMC

AMC is the ABI Metadata Compiler/toolchain. This is the standalone AMC repository.

## Documentation

- [AMC toolchain and CLI](docs/amc/amc.md)
- [AMC 工具链与 CLI](docs/amc/amc_zh.md)

## Standalone build

AMC prefers an installed ABIX package. It does not need the ABIX source tree.

```bash
cmake -S . -B build \
  -DABIX_DIR=/opt/abix/lib/cmake/ABIX \
  -DCMAKE_PREFIX_PATH="/opt/abix;/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /opt/amc
```

If a required dependency is missing or too old, CMake uses `FetchContent` to place it in the repository-local `dependencies/` directory. ABIX and MICS are cloned; fmt and toml++ are downloaded as pinned release tarballs (fmt 11.2.0 and toml++ 3.4.0 by default), which avoids distribution packages with an older API:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DAMC_ABIX_REF=devel \
  -DAMC_MICS_REF=main \
  -DAMC_FMT_VERSION=11.2.0 \
  -DAMC_TOMLPLUSPLUS_VERSION=3.4.0
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`dependencies/` is a cacheable, untracked dependency checkout; only [`dependencies/README.md`](dependencies/README.md) is tracked. In this mode AMC still builds and runs its tests, but installs only the tools and headers (not the `AMCConfig.cmake` package export), because the package export requires installed ABIX and fmt packages. Reconfigure with `-DABIX_DIR=...` and an installed fmt to install the package.

Required packages:

- `ABIX 0.3` runtime and MICS
- LLVM and Clang (`clang-cpp`)
- fmt
- toml++
- Catch2 v3 for the C++ tests (optional)
- Lua 5.4 for the optional Aue conformance prototype

AMC owns the artifact-layer libraries (`format`, `abi`, `metadata`, `tools`) and exports them as `AMC::format`, `AMC::abi`, `AMC::metadata` and `AMC::tools`. They are compiled here rather than borrowed from ABIX, because their public headers and namespace are AMC's `SKL/AMC/Core` API. ABIX runtime remains header-only; AMC is the toolchain that needs LLVM/Clang and keeps its own exception/RTTI policy.

The standalone CTest suite covers the C++ core/e2e tests, the installed package consumer, feature tests, Rust provider/panic tests, cancellation, interop, capability generation and an independent CMake integration test. When Lua 5.4 is available, `Aue` also builds the L0/L1 conformance runner and its tests. The old monorepo-only `amc-integration-test` and `amc-cmdly-interop-test` were retired with the split.

## Installed package

```cmake
find_package(AMC 0.1 CONFIG REQUIRED)

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
