# Zig

<p align="center">
  <a href="README_ZH.md">中文</a> · English
</p>

Zig is a **backend-only** ABIX target: ABIX consumes an existing `.abix` artifact and projects it to Zig. There is no Zig frontend and no Zig-specific model in AMC.

## What you get

- `extern struct` records with layout-compatible fields.
- `extern fn` declarations for the exported symbols.
- `[*c]` typed pointers and opaque handles.
- `comptime` size / align / offset assertions.

## Role

- `amc --describe-language zig` reports `backend protocol=jsonl-v1`.
- `amc-zig` is a standalone provider process; it consumes the same ABIX IR as the Rust and C++ backends.
- Zig is used as a projection target, not as an import language. Build the artifact from a C++ or Rust source first, then run `amc generate -l zig`.

## Generated projection

```bash
amc generate build/package.abix -l zig -o gen/
```

`amc_generated.zig` contains:

- `extern struct` declarations for layout-compatible records;
- `extern fn` declarations for exported functions;
- `[*c]` typed pointers, with nullable pointers for opaque pointees;
- `ABIX_*` constants for type identity, size, align and field offsets;
- a `comptime` block with `@sizeOf`, `@alignOf` and `@offsetOf` assertions.

The same artifact that drives the Rust projection drives the Zig projection. `tests/interop/` builds one native library and runs both consumers against it; no Zig-specific information is added to AMC.

## Next

- [Quick start](quickstart.md)
- [Advanced: layout checks and limits](advanced.md)
