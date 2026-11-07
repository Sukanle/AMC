# Aue

Aue is the Lua 5.4 integration used to validate the ABIX contract from a dynamic language. It is an integration/prototype, not part of AMC's Frontend/IR/Pass/Backend core:

```text
Lua VM          standard Lua 5.4 runtime
ABIX Contract   generated from an `.abix` artifact (amc generate -l lua)
Native Execution C functions exported by the host module
```

## Layers

| Layer        | Dispatch                                         | Purpose                |
| ------------ | ------------------------------------------------ | ---------------------- |
| L0 `default` | functions copied into the module table eagerly   | golden reference       |
| L1 `meta`    | empty module table + `__index` contract dispatch | contract-driven access |
| L2 `native`  | reserved for VM inline dispatch                  | not compiled in yet    |

The runtime may only downgrade: requesting L2 on a build with L1 compiled in selects L1. `abix-conformance` checks that L0 and L1 produce byte-identical observable output.

## Contract shape

`amc generate file.abix -l lua -o contract.hpp` emits an `aue::Contract` with:

- the package name and ABI hash;
- function entries (`name`, arity, native symbol); and
- record type/field descriptors for direct field access.

L1 builds its name → entry index once at registration, so a cold lookup is a single table lookup rather than a scan of the contract. Field metatables use the same approach: `__index`/`__newindex` resolve a pre-built field index and read/write the native object at the descriptor offset.

Field kinds cover scalars, pointers, records, arrays and opaque values. A record field returns a non-owning view that keeps its parent userdata alive through Lua uservalue 1; an array field returns a view with `#view` and 1-based element access when the contract describes the element type. Opaque and undescribed fields resolve to `nil`; writes to record/array/opaque fields are rejected instead of guessing an encoding.

The generated metatable name is `<module>.<type>` (for example `counter.Counter`), matching the name the native module uses with `luaL_setmetatable`. Array descriptors use `type_index` for record elements or `element_kind`/`element_size` for scalar elements; the current generator infers scalar array elements from the ABI type spelling (e.g. `int[4]`) and leaves unknown element types read-only at the length-only level.

## Native lifetime

`register_contract_types` installs a default no-op `__gc` hook. A type that owns native resources overrides it with `register_type_destroy()`:

```cpp
aue::register_type_destroy(state, "counter.Counter", counter_destroy);
```

The hook receives the userdata's native pointer and runs before Lua releases the block. The demo `Counter` registers a destructor that maintains the live count exposed as `counter.live_count()`; the conformance script creates a temporary object, drops it, runs `collectgarbage`, and asserts the count returns to its starting value.

## L2 native dispatch prototype

`-DAUE_ENABLE_L2=ON` compiles `Level::l2`. It is a research prototype, not a Lua VM patch:

- registration builds a C-side open-addressed hash index over the contract;
- `__index` performs native probing and deliberately does not memoize;
- every access therefore crosses the C dispatch path, which makes the benchmark comparable with L1's cold path.

The conformance runner checks that L2 does not eagerly bind and that a rawget still misses after resolution. L2 is opt-in so the default build keeps the stable L0/L1 surface.

## Running

From an AMC build with Lua available:

```bash
cmake --build <build-dir> --target abix-conformance abix-bench
<b>bin/abix-conformance Aue/Conformance.lua
<b>bin/abix-conformance Aue/Edges.lua
<b>bin/abix-bench
```

Standalone, without the AMC driver or generated contract (requires installed ABIX runtime, fmt and Lua 5.4):

```bash
cmake -S Aue/standalone -B build/aue -DABIX_DIR=<abix-package>
cmake --build build/aue --parallel
ctest --test-dir build/aue --output-on-failure
cmake --install build/aue --prefix <install-prefix>
(cd build/aue && cpack)   # Aue-<version>-<system>.tar.gz
```

The installed layout is `bin/abix-conformance`, `bin/abix-bench` and `share/aue/{README.md,Conformance.lua,Edges.lua}`. The standalone path uses the checked-in fallback contract in `Aue/src/Counter.cpp`; the AMC build generates the contract from `Counter.abic.toml` instead. The conformance runner executes the same Lua script under L0 and L1 and fails if the output differs. The benchmark is a smoke test that reports per-call overhead; its numbers are not asserted.
