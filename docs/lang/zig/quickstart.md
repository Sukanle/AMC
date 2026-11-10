# Zig quick start

<p align="center">
  <a href="quickstart_ZH.md">中文</a> · English
</p>

Zig consumes an artifact built from another language. Build a C++ or Rust provider first, then generate the Zig projection.

## Prerequisites

- A built AMC tree
- A `.abix` artifact from a C++ or Rust provider
- A Zig toolchain matching the provider target

## Generate

```bash
amc generate build/package.abix -l zig -o gen/
```

## Consume

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

Compile the consumer for the same target and link the same native library as the provider. No C header or generated C shim is needed.

## Next

- [Advanced: layout checks and limits](advanced.md)
- [Zig overview](README.md)
