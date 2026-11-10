# C++ advanced topics

<p align="center">
  <a href="advanced_ZH.md">中文</a> · English
</p>

Projection details, the header-only interface layer and the implementation layout.

## Interface layer

The C++ interface layer is header-only and lives in [`SKL/ABIX/Interface/`](https://github.com/Sukanle/ABIX/tree/main/include/SKL/ABIX/Interface):

- `Contract.h`: `InterfaceId`, `method_signature`, `interface_id`, `InterfaceMethod` and `InterfaceImplementation`.
- `Proxy.h`: `UniqueInterfaceProxy<Traits>`; a generated or hand-written proxy gives `shape.area()`.
- `Registry.h` / `Ref.h`: in-process registration and one-time resolution.
- `Module.h` / `ModuleRegistry.h` / `Lease.h` / `Discovery.h`: dynamic module manifests and leases.
- `Owner.h`: cross-module ownership.
- `Declaration.h`: declaration sugar (`SKL_ABIX_INTERFACE_DECLARE`) and version packing helpers.

A provider keeps its classes, virtual functions and layout. The adapter is the only bridge, and the interface never feeds `TypeID` / `LayoutHash` / `ABIHash`.

## Implementation layout

The provider is split by concern so the Clang frontend and the emitter backend stay maintainable:

| Path                                    | Responsibility                                     |
| --------------------------------------- | -------------------------------------------------- |
| `include/SKL/AMC/Frontend/Cpp/Cpp.h`    | public frontend entry (`run_frontend`)             |
| `include/SKL/AMC/Backend/Cpp/Cpp.h`     | public backend entry (`run_backend`)               |
| `include/SKL/AMC/Private/Frontend/Cpp/` | `Native`, `Extractor`, `ThunkEmitter` declarations |
| `src/SKL/AMC/Private/Frontend/Cpp/`     | Clang visitor and thunk implementation             |
| `include/SKL/AMC/Private/Backend/Cpp/`  | emitter declarations                               |
| `src/SKL/AMC/Private/Backend/Cpp/`      | projection/interface/handle/error/check emitters   |
| `CodeIn/Cpp/Prologue.hpp`               | runtime-loaded generated prologue                  |

`Extractor` is a non-owning Clang visitor; `ThunkEmitter` is a non-owning operation object. The C++ backend entry only loads the artifact, applies lowering and calls the emitters. Private headers are installed only with `-DAMC_INSTALL_PRIVATE_HEADERS=ON`.

## Generated projection details

```bash
amc generate build/package.abix -l cpp -o gen/
```

`amc_generated.hpp` contains layout-compatible C++ types and typedefs, ABI constants, ownership and error-boundary helpers, and metadata-region accessors. For an interface contract it also emits `k<Name>Interface`, `k<Name>Version`, the `<Name>_<method>_fn` typedefs, a `Traits` struct with a typed zero-based `Slots` table and expected signatures, and, when every method takes the object handle first, an `UniqueInterfaceProxy`-derived proxy.

`amc_abi_check.hpp` is the compile-time layout check. Include it after the native declarations it names:

```cpp
#include "demo_api.hpp"
#include "amc_abi_check.hpp"
```

It asserts `sizeof`, `alignof`, per-field `offsetof` and per-field width. Bit-fields and implicitly named types are skipped because `offsetof` / `sizeof` are not defined for them.

## Limits and notes

- C++ STL types must not cross the boundary raw; declare a `[[view]]`, `[[instantiate]]` or `[[expose]]` mapping, or use an ABIX-STD contract.
- The recorded ABI is target-specific. Use the same target, compiler ABI and calling convention on both sides.
- The interface layer does not require inheritance or virtual functions; a plain `struct` works.
- The generated C++ header targets C++17.
- The Clang frontend requires LLVM / Clang development libraries; the Rust and Zig providers do not.
