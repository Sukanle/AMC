# C++

<p align="center">
  <a href="README.md">English</a> · 中文
</p>

C++ 是 ABIX 的第一个宿主语言，也是参考 provider。它使用 Clang/LLVM 从 AST 提取 ABI 事实，并生成带编译期布局检查的 C++17 投影。

## 你会得到什么

- `amc_generated.hpp`：布局兼容的 C++17 类型、ABI 常量、所有权/错误辅助代码与接口契约。
- `amc_abi_check.hpp`：编译期 `sizeof` / `alignof` / `offsetof` 断言。
- header-only 的接口层，包含 proxy、registry 与 lease。

## 角色

- `amc-cpp` 同时是 frontend 与 backend provider (`amc --describe-language cpp` 报告 `frontend backend`)。
- 前端用 Clang 解析配置的翻译单元；AMC 把类型、字段、函数、符号、布局、target 事实、所有权与错误契约记录进 artifact。
- 后端生成 `amc_generated.hpp` 与 `amc_abi_check.hpp`。
- C++ 同时也是 ABIX 自身与 AMC core 的实现语言。

## 前端

`.abic.toml` 选择 `language = "cpp"`，并列出文件与导出符号：

```toml
[[import]]
language = "cpp"
flags = ["-std=c++17"]
files = ["demo_api.hpp"]
symbols = ["DemoVec2", "demo_add", "demo_div"]
```

前端与 AMC 共同处理模板、`std::` 类型、属性、`[[interface]]` / `[[implements]]`、`[[owned]]` / `[[contract]]`、`[[instantiate]]`、`[[view]]`、`[[expose]]` 与错误边界。详见 [`abic_zh.md`](../../abic/abic_zh.md)。

## 生成的投影

`amc generate -l cpp` 会生成 `amc_generated.hpp` 与 `amc_abi_check.hpp`。前者包含布局兼容类型、ABI 常量、所有权/错误 helper 与接口契约；后者在编译期断言 `sizeof`、`alignof` 与逐字段 `offsetof`。

接口/proxy 输出、检查细节与实现布局见[高级特性](advanced_ZH.md)。

## 下一步

- [快速开始](quickstart_ZH.md)
- [高级特性：接口、实现与限制](advanced_ZH.md)
- [`.abic.toml` 参考](../../abic/abic_zh.md)
- [用户迁移成本预览](../migration_ZH.md)
