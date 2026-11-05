# L3 Adaptation Examples

L3 is the explicit adaptation layer: the producer/library owns semantics that cannot be inferred from a plain C ABI. AMC records those rules as contracts and the generated bindings expose them without guessing.

## Patterns

| L3 concern | Contract |
| --- | --- |
| owner handle | `[[owned]]` + `[[contract]]` |
| borrowed return | `[[contract]]` with `returns = "borrowed"` |
| callback | ABIX-STD callback type or an explicit function-pointer contract |
| named errors | `[[error-set]]` + `[[error]]` |
| handle-shaped interface | `[[interface]]` + `[[implements]]` |
| native STL parameter | `[[adapt]]` |

## Example: M7 fixture

`tests/interop/m7/` is a self-contained L3 example with one native library and one `.abix` contract:

- `l3_session_create` / `l3_session_destroy` are recorded as `[[owned]]`;
- `l3_session_div` has a named `M7Errors` error set and a typed `Result`;
- `l3_session_subscribe` passes a caller-owned callback;
- `L3SessionOps` binds `l3_session_get` / `l3_session_set` as a handle-shaped interface;
- C++, Rust and Zig consumers use the same native library and the same `m7.abix`.

Run it directly:

```bash
python3 tests/interop/m7/m7_test.py build/Release/bin .
```

The script also covers negative cases:

- target mismatch is rejected by `amc target`;
- a stale `.lowering` sidecar is rejected by `amc generate`;
- a native header with a different layout is rejected by the generated ABI check at compile time;
- interface version mismatch is rejected at bind time;
- cross-artifact interface version drift is reported by `amc verify`;
- the generated proxy releases its `InterfaceLease` on drop/deinit.

## Example: real project (cmdly)

`tests/interop/cmdly/` wraps the C++-only cmdly line editor in a thin C ABI facade, then declares:

- an owned terminal handle;
- a borrowed output string;
- a cross-language key callback with caller-owned context.

The Rust consumer never sees cmdly's C++ classes, `std::shared_ptr`, templates or STL layout; it sees only the recorded contracts.

## Design rule

L3 never means "infer an adapter from a type name". It means:

1. the user declares the contract;
2. AMC validates that the contract can be represented safely;
3. the backend emits a typed wrapper or rejects generation.

If a type cannot be proven safe, `amc build` / `amc generate` must fail with a diagnostic instead of silently degrading to raw bytes.
