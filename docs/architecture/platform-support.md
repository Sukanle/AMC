# Platform support

<p align="center">
  <a href="platform-support_zh.md">中文</a> · English
</p>

## Supported platforms

AMC and ABIX target **64-bit platforms only**.

| Pointer width | Status                               |
| ------------- | ------------------------------------ |
| 64-bit        | Supported                            |
| 32-bit        | Not supported; deferred indefinitely |
| 16-bit        | Not supported                        |

32-bit support is not part of the current roadmap. It is not planned for v0.4.x or v0.5.0, and AMC does not treat a 32-bit target as a best-effort configuration. The Rust frontend rejects `[target] pointer_bits != 64` with an explicit diagnostic before it projects any ABI.

## Why 32-bit is out of scope

### 64-bit and 128-bit identity hashes are structural

AMC/ABIX use 64-bit and 128-bit hashes for type identity, layout identity, function signatures, artifact identity, and metadata. These are not optional cache keys: they are the stable identity model carried by `.abix`, `.semantic`, `.lowering`, and the generated metadata region.

On a 32-bit platform:

- native 64-bit arithmetic is emulated or requires helper routines;
- truncating identities to 32 bits would create collisions and break stable identity;
- redesigning the identity and hash model for 32-bit would change the wire formats and every consumer.

This makes 32-bit support a redesign of the core identity model, not a portability flag.

### ABIX metadata and query tables are pointer-rich

ABIX records type, field, function, symbol, interface, error, and canonical-layout tables. The query path also keeps module and metadata state. A 32-bit process has a much smaller address space, and the table-heavy path has significantly higher pressure:

- pointer-rich records consume a larger fraction of the address space;
- large modules and multi-module queries can fragment or exhaust the 32-bit address space;
- the metadata region and query working set have less headroom, so performance and memory pressure are materially worse than on 64-bit;
- 64-bit module instances, lease tokens, and owner handles require 64-bit loads/stores and emulation on many 32-bit targets.

The problem is not only speed; the address-space budget changes the design of the metadata and query tables.

### The owner and lease ABI is 64-bit

`abix_owner_handle.module_instance`, `abix_lease`, and `abix_module_instance` are `uint64_t`. The owner/lease lifecycle assumes a 64-bit identity space and a target ABI with a stable 64-bit integer representation. Supporting 32-bit would require per-target owner/owned-view layouts and lease handling on top of the existing model.

### Test and maintenance cost

Supporting 32-bit means at least:

- i386 SysV, ARMv7 AAPCS, and Windows x86 ABI variants;
- target-specific owner/owned-view layouts and ABI checks;
- cross-target backend tests and CI runners;
- ongoing debugging for a configuration with no current user demand.

The maintenance cost is not justified by current demand.

## What "not supported" means

- No 32-bit CI or test matrix.
- No guarantee that generated projections, owner/owned-view layouts, metadata, or query results are correct on 32-bit.
- A 32-bit target is expected to fail closed (`pointer_bits != 64`) rather than produce best-effort output.
- Bug reports specific to 32-bit execution are out of scope.

## Revisiting 32-bit

If 32-bit is revisited, it needs a design proposal covering:

1. the 64-bit/128-bit identity and hash strategy;
2. metadata/query table layout and address-space budget;
3. owner/owned-view/lease ABI layout per target ABI;
4. target-aware backend layout and code generation;
5. a real cross-target test matrix.

Until then, 32-bit remains indefinitely deferred and is not part of the mainline roadmap.

## Related

- [ABIX lease ABI](https://github.com/Sukanle/ABIX/blob/main/docs/abix/lease.md)
- [ABIX STD layout](https://github.com/Sukanle/ABIX/blob/main/docs/abix/std.md)
