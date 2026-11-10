# Zig 高级特性

<p align="center">
  <a href="advanced.md">English</a> · 中文
</p>

布局校验、跨语言互操作与当前限制。

## 布局检查

`comptime` 块会把布局不匹配变成构建失败：

```zig
comptime {
    if (@sizeOf(DemoVec2) != 16) @compileError("DemoVec2 size mismatch");
    if (@alignOf(DemoVec2) != 8) @compileError("DemoVec2 align mismatch");
    if (@offsetOf(DemoVec2, "y") != 8) @compileError("DemoVec2::y offset mismatch");
}
```

这是 C++ `static_assert` 与 Rust `const assert!` 在 Zig 中的对应物：记录的 ABI 在编译期被验证，原生调用还来不及出错。

## 跨语言互操作

驱动 Rust 投影的同一个 artifact 也驱动 Zig 投影。`tests/interop/` 构建一个原生库，并让两种消费者都对它运行；AMC 中没有加入 Zig 专属信息。

## 限制与说明

- 目前 Zig 是 backend-only：没有 Zig 源码提取器，因此 Zig provider 必须通过其他前端或生成的 artifact 暴露 ABI 兼容边界。
- 导出面仍必须能用原生 ABI 表达；Zig 内部类型与 allocator 留在模块内部。
- 记录的 ABI 是 target 相关的；Zig 消费方必须按与 provider 相同的 target 与 C ABI 构建。
- `[*c]` 指针保留 C 指针语义，`extern struct` 与 `comptime` 检查则把布局钉死。
- 错误边界尚未投影为 Zig error set / 错误联合；当前只保留普通 status/payload 参数。
- ABIX IR 中没有加入任何 Zig 专属概念；投影完全由 artifact 推导。
