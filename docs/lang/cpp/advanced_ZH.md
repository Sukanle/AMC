# C++ 高级特性

<p align="center">
  <a href="advanced.md">English</a> · 中文
</p>

投影细节、header-only 接口层与实现布局。

## 接口层

C++ 接口层是 header-only 的，位于 [`SKL/ABIX/Interface/`](https://github.com/Sukanle/ABIX/tree/main/include/SKL/ABIX/Interface)：

- `Contract.h`：`InterfaceId`、`method_signature`、`interface_id`、`InterfaceMethod` 与 `InterfaceImplementation`。
- `Proxy.h`：`UniqueInterfaceProxy<Traits>`；生成或手写的代理提供 `shape.area()`。
- `Registry.h` / `Ref.h`：进程内注册与一次性解析。
- `Module.h` / `ModuleRegistry.h` / `Lease.h` / `Discovery.h`：动态 module manifest 与租约。
- `Owner.h`：跨模块所有权。
- `Declaration.h`：声明糖（`SKL_ABIX_INTERFACE_DECLARE`）与版本打包辅助宏。

provider 保留自己的 class、虚函数与布局。适配器是唯一的桥；接口从不参与 `TypeID` / `LayoutHash` / `ABIHash`。

## 实现布局

provider 按职责拆分，避免 Clang 前端和 emitter 后端重新变成单文件：

| 路径                                    | 职责                                            |
| --------------------------------------- | ----------------------------------------------- |
| `include/SKL/AMC/Frontend/Cpp/Cpp.h`    | 公共前端入口（`run_frontend`）                  |
| `include/SKL/AMC/Backend/Cpp/Cpp.h`     | 公共后端入口（`run_backend`）                   |
| `include/SKL/AMC/Private/Frontend/Cpp/` | `Native`、`Extractor`、`ThunkEmitter` 声明      |
| `src/SKL/AMC/Private/Frontend/Cpp/`     | Clang visitor 与 thunk 实现                     |
| `include/SKL/AMC/Private/Backend/Cpp/`  | emitter 声明                                    |
| `src/SKL/AMC/Private/Backend/Cpp/`      | projection/interface/handle/error/check emitter |
| `CodeIn/Cpp/Prologue.hpp`               | 运行时加载的生成 prologue                       |

`Extractor` 是不持有资源的 Clang visitor；`ThunkEmitter` 是不持有资源的操作类。 C++ backend 入口只负责加载 artifact、应用 lowering 并调用 emitter。私有头文件只有在 `-DAMC_INSTALL_PRIVATE_HEADERS=ON` 时才会安装。

## 生成的投影细节

```bash
amc generate build/package.abix -l cpp -o gen/
```

`amc_generated.hpp` 包含布局兼容的 C++ 类型与 typedef、ABI 常量、所有权与错误边界 helper，以及 metadata region 访问器。对接口契约，它还会生成 `k<Name>Interface`、`k<Name>Version`、`<Name>_<method>_fn` typedef、一个带类型化零基 `Slots` 表与期望签名的 `Traits` struct，以及当每个方法首参都是对象句柄时的 `UniqueInterfaceProxy` 派生代理。

`amc_abi_check.hpp` 是编译期布局检查。请把它放在它所引用的原生声明之后：

```cpp
#include "demo_api.hpp"
#include "amc_abi_check.hpp"
```

它断言 `sizeof`、`alignof`、逐字段 `offsetof` 与逐字段宽度。位域与隐式命名的类型会被跳过，因为 `offsetof` / `sizeof` 对它们没有定义。

## 限制与说明

- C++ STL 类型不能原样跨越边界；需要声明 `[[view]]`、`[[instantiate]]` 或 `[[expose]]` 映射，或使用 ABIX-STD 契约。
- 记录的 ABI 是 target 相关的。两端应使用相同的 target、编译器 ABI 与调用约定。
- 接口层不要求继承或虚函数；普通 `struct` 即可。
- 生成的 C++ header 面向 C++17。
- Clang 前端需要 LLVM / Clang 开发库；Rust 与 Zig provider 不需要。
