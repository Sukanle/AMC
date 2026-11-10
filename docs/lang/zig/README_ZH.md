# Zig

<p align="center">
  <a href="README.md">English</a> · 中文
</p>

Zig 是 **backend-only** 的 ABIX 目标：ABIX 消费已有的 `.abix` artifact，再把它投影成 Zig。没有 Zig 前端，AMC 中也没有 Zig 专属模型。

## 你会得到什么

- 布局兼容的 `extern struct` 记录。
- 导出符号的 `extern fn` 声明。
- `[*c]` 类型指针与 opaque handle。
- `comptime` 的 size / align / offset 断言。

## 角色

- `amc --describe-language zig` 报告 `backend protocol=jsonl-v1`。
- `amc-zig` 是独立的 provider 进程；它消费与 Rust、C++ 后端相同的 ABIX IR。
- Zig 是投影目标，而不是 import 语言。先从 C++ 或 Rust 源码构建 artifact，再运行 `amc generate -l zig`。

## 生成的投影

```bash
amc generate build/package.abix -l zig -o gen/
```

`amc_generated.zig` 包含：

- 布局兼容记录的 `extern struct` 声明；
- 导出函数的 `extern fn` 声明；
- `[*c]` 带类型指针；不透明 pointee 使用可空指针；
- 类型身份、大小、对齐与字段 offset 的 `ABIX_*` 常量；
- 带 `@sizeOf`、`@alignOf` 与 `@offsetOf` 断言的 `comptime` 块。

驱动 Rust 投影的同一份 artifact 也驱动 Zig 投影。`tests/interop/` 会构建同一个原生库，并让两个消费者都对它运行；AMC 中没有加入任何 Zig 专属信息。

## 下一步

- [快速开始](quickstart_ZH.md)
- [高级特性：布局检查与限制](advanced_ZH.md)
