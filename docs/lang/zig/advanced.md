# Zig advanced topics

<p align="center">
  <a href="advanced_ZH.md">中文</a> · English
</p>

Layout verification, cross-language interop and current limits.

## Layout checks

The `comptime` block turns a layout mismatch into a build failure:

```zig
comptime {
    if (@sizeOf(DemoVec2) != 16) @compileError("DemoVec2 size mismatch");
    if (@alignOf(DemoVec2) != 8) @compileError("DemoVec2 align mismatch");
    if (@offsetOf(DemoVec2, "y") != 8) @compileError("DemoVec2::y offset mismatch");
}
```

This is the Zig counterpart of the C++ `static_assert` checks and the Rust `const assert!` checks: the recorded ABI is verified at compile time, before the native call can go wrong.

## Cross-language interop

The same artifact that drives the Rust projection drives the Zig projection. `tests/interop/` builds one native library and runs both consumers against it; no Zig-specific information is added to AMC.

## Limits and notes

- Zig is backend-only today: there is no Zig source extractor, so a Zig provider must expose an ABI-compatible boundary through another frontend or a generated artifact.
- The exported surface still has to be representable in the native ABI; internal Zig types and allocators stay inside the module.
- The recorded ABI is target-specific; build the Zig consumer for the same target and C ABI as the provider.
- `[*c]` pointers keep C pointer semantics, while `extern struct` and `comptime` checks pin the layout.
- Error boundaries are not projected to Zig error sets / error unions yet; the current output keeps the plain status/payload parameters.
- No Zig-specific concept is added to ABIX IR; the projection is derived only from the artifact.
