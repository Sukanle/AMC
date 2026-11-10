# C++ quick start

<p align="center">
  <a href="quickstart_ZH.md">中文</a> · English
</p>

This walkthrough builds the interop demo, generates the C++ projection and compiles a consumer that links the native library.

## Prerequisites

- CMake and a C++17 compiler
- LLVM / Clang development libraries (the C++ frontend uses Clang AST)
- A built AMC tree

## Build and generate

Build the interop demo and generate its C++ projection:

```bash
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel

amc build -c tests/interop/interop_demo.abic.toml -B build/demo
amc generate build/demo/interop_demo.abix -l cpp -o build/demo/gen
```

The generated header can then be included by a C++ consumer; `amc_abi_check.hpp` fails the build if the provider's layout drifted.

## Consume the projection

```cpp
#include "demo_api.hpp"
#include "amc_generated.hpp"
#include "amc_abi_check.hpp"

int main() {
    DemoVec2 v{4, 2.5};
    if (demo_add(2, 3) != 5) return 1;
    if (demo_scale(&v, 2.0) != 13.0) return 1;
    return 0;
}
```

`amc_abi_check.hpp` must be included after the native declarations it names. Link the same native library that produced the artifact.

## Next

- [Advanced: interfaces, implementation and limits](advanced.md)
- [C++ overview](README.md)
