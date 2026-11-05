# Aue

Aue is the Lua 5.4 integration used to validate the ABIX contract from a
dynamic language. It is an integration/prototype, not part of AMC's
Frontend/IR/Pass/Backend core:

```text
Lua VM          standard Lua 5.4 runtime
ABIX Contract   generated from an `.abix` artifact (amc generate -l lua)
Native Execution C functions exported by the host module
```

## Layers

| Layer | Dispatch | Purpose |
| --- | --- | --- |
| L0 `default` | functions copied into the module table eagerly | golden reference |
| L1 `meta` | empty module table + `__index` contract dispatch | contract-driven access |
| L2 `native` | reserved for VM inline dispatch | not compiled in yet |

The runtime may only downgrade: requesting L2 on a build with L1 compiled in
selects L1. `abix-conformance` checks that L0 and L1 produce byte-identical
observable output.

## Contract shape

`amc generate file.abix -l lua -o contract.hpp` emits an `aue::Contract` with:

- the package name and ABI hash;
- function entries (`name`, arity, native symbol); and
- record type/field descriptors for direct field access.

L1 builds its name → entry index once at registration, so a cold lookup is a
single table lookup rather than a scan of the contract. Field metatables use
the same approach: `__index`/`__newindex` resolve a pre-built field index and
read/write the native object at the descriptor offset. Unsupported field kinds
resolve to `nil`; writes to unsupported kinds raise a Lua error instead of
guessing an encoding.

The generated metatable name is `<module>.<type>` (for example
`counter.Counter`), matching the name the native module uses with
`luaL_setmetatable`.

## Native lifetime

Field metatables install a `__gc` hook. The demo `Counter` is a POD userdata,
so the hook is currently a no-op; the hook is the integration point for native
objects that own resources and is covered by the conformance fixture.

## Running

From an AMC build with Lua available:

```bash
cmake --build <build-dir> --target abix-conformance abix-bench
<b>bin/abix-conformance Aue/Conformance.lua
<b>bin/abix-conformance Aue/Edges.lua
<b>bin/abix-bench
```

The conformance runner executes the same Lua script under L0 and L1 and fails
if the output differs. The benchmark is a smoke test that reports per-call
overhead; its numbers are not asserted.
