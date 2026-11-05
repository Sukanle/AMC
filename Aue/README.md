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

`register_contract_types` installs a default no-op `__gc` hook. A type that owns
native resources overrides it with `register_type_destroy()`:

```cpp
aue::register_type_destroy(state, "counter.Counter", counter_destroy);
```

The hook receives the userdata's native pointer and runs before Lua releases
the block. The demo `Counter` registers a destructor that maintains the live
count exposed as `counter.live_count()`; the conformance script creates a
temporary object, drops it, runs `collectgarbage`, and asserts the count
returns to its starting value.

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
