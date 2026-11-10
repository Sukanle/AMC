# C++

<p align="center">
  <a href="README_ZH.md">中文</a> · English
</p>

C++ is ABIX's first host language and the reference provider. It uses Clang/LLVM to extract ABI facts from the AST and generates a C++17 projection with compile-time layout checks.

## What you get

- `amc_generated.hpp`: layout-compatible C++17 types, ABI constants, ownership/error helpers and interface contracts.
- `amc_abi_check.hpp`: compile-time `sizeof` / `alignof` / `offsetof` assertions.
- A header-only interface layer with proxies, registries and leases.

## Role

- `amc-cpp` is both a frontend and a backend provider (`amc --describe-language cpp` reports `frontend backend`).
- The frontend parses the configured translation units with Clang; AMC records types, fields, functions, symbols, layout, target facts, ownership and error contracts into the artifact.
- The backend emits `amc_generated.hpp` and `amc_abi_check.hpp`.
- C++ is also the implementation language of ABIX itself and of the AMC core.

## Frontend

`.abic.toml` selects `language = "cpp"` and lists the files and exported symbols:

```toml
[[import]]
language = "cpp"
flags = ["-std=c++17"]
files = ["demo_api.hpp"]
symbols = ["DemoVec2", "demo_add", "demo_div"]
```

The frontend and AMC handle templates, `std::` types, attributes, `[[interface]]` / `[[implements]]`, `[[owned]]` / `[[contract]]`, `[[instantiate]]`, `[[view]]`, `[[expose]]` and the error boundary. See [`abic.md`](../../abic/abic.md).

## Generated projection

`amc generate -l cpp` emits `amc_generated.hpp` and `amc_abi_check.hpp`. The first contains layout-compatible types, ABI constants, ownership/error helpers and interface contracts; the second asserts `sizeof`, `alignof` and per-field `offsetof` at compile time.

See [advanced topics](advanced.md) for the interface/proxy output, check details and the implementation layout.

## Next

- [Quick start](quickstart.md)
- [Advanced: interfaces, implementation and limits](advanced.md)
- [`.abic.toml` reference](../../abic/abic.md)
- [Migration cost preview](../migration.md)
