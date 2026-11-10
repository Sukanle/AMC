# Zig 快速开始

<p align="center">
  <a href="quickstart.md">English</a> · 中文
</p>

Zig 消费由其他语言构建的 artifact。先构建 C++ 或 Rust provider，再生成 Zig 投影。

## 前置条件

- 已构建的 AMC
- 由 C++ 或 Rust provider 生成的 `.abix` artifact
- 与 provider target 一致的 Zig 工具链

## 生成

```bash
amc generate build/package.abix -l zig -o gen/
```

## 使用

```zig
const std = @import("std");
const abi = @import("gen/amc_generated.zig");

pub fn main() void {
    const sum = abi.demo_add(2, 3);
    if (sum != 5) @panic("demo_add");

    var v = abi.DemoVec2{ .x = 4, .y = 2.5 };
    const scaled = abi.demo_scale(&v, 2.0);
    if (scaled != 13.0) @panic("demo_scale");

    var out: c_int = 0;
    const rc = abi.demo_div(10, 2, &out);
    if (rc != 0 or out != 5) @panic("demo_div");
}
```

按与 provider 相同的 target 编译消费者，并链接同一个原生库。不需要 C 头文件或生成的 C shim。

## 下一步

- [高级特性：布局检查与限制](advanced_ZH.md)
- [Zig 概览](README_ZH.md)
