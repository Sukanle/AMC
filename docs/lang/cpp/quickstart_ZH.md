# C++ 快速开始

<p align="center">
  <a href="quickstart.md">English</a> · 中文
</p>

本流程构建 interop demo、生成 C++ 投影，并编译一个链接原生库的消费者。

## 前置条件

- CMake 与 C++17 编译器
- LLVM / Clang 开发库（C++ 前端使用 Clang AST）
- 已构建的 AMC

## 构建并生成

构建 interop demo 并生成 C++ 投影：

```bash
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja -S .
cmake --build build/Release --parallel

amc build -c tests/interop/interop_demo.abic.toml -B build/demo
amc generate build/demo/interop_demo.abix -l cpp -o build/demo/gen
```

生成的 header 可以被 C++ 消费方直接 include；如果 provider 布局发生漂移，`amc_abi_check.hpp` 会让构建失败。

## 使用投影

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

`amc_abi_check.hpp` 必须在它引用的原生声明之后 include。链接生成 artifact 的同一个原生库。

## 下一步

- [高级特性：接口、实现与限制](advanced_ZH.md)
- [C++ 概览](README_ZH.md)
