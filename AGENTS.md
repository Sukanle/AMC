# AGENTS.md — AMC Agent Constraints

AMC is an **interface compiler and code-generation infrastructure**: it understands source-language interfaces, lowers them into ABIX IR, validates the result and projects it through language backends. It compiles interfaces rather than whole programs and does not replace the user's compiler or linker, but its internal implementation is expected to use compiler infrastructure such as LLVM/Clang where that is the right tool.

Read `docs/architecture/module-boundaries.md` before changing module dependencies. The dependency direction is:

```text
Frontend -> IR -> Pass -> Backend
                 \-> Compilation -> Interfaces
```

Do not introduce a dependency that points back from IR/Pass/Backend to a specific frontend, a CLI command or a Clang AST.

---

## LLVM / Clang usage

AMC is moving toward compiler and code-generation infrastructure. LLVM and Clang are first-class dependencies, not provider-only implementation details. Any AMC target may include and link LLVM/Clang headers and libraries when they are the right tool for the job.

LLVM's `llvm/ADT` and `llvm/Support` contain production-tested replacements for parts of the C++ standard library. Prefer them where they improve allocation behaviour, lifetime clarity or LLVM API interoperability.

### Scope

LLVM/Clang APIs may be used across AMC modules:

- `Frontend/*`
- `IR/*`
- `Pass/*`
- `Backend/*`
- `Compilation/*`
- `Diagnostics/*`
- `Query/*`
- `Support/*`
- tools, providers and tests

This does **not** remove the module dependency direction above. IR/Pass/Backend should still avoid depending on a specific frontend or CLI command. Using LLVM library facilities is allowed; coupling a language-neutral layer to one frontend's Clang AST remains a module-boundary decision and must be explicit.

### Preferred replacements

| Standard library                              | LLVM facility                            | Use when                                                                                                     |
| --------------------------------------------- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `std::vector`                                 | `llvm::SmallVector<T, N>`                | Small, frequently created containers; choose `N` from measured usage, not by habit                           |
| `const char*` / `std::string_view` parameters | `llvm::StringRef`                        | Non-owning read-only strings; never store beyond the source lifetime                                         |
| `std::unordered_map`                          | `llvm::DenseMap`                         | Pointer/integer keys and cache-friendly iteration                                                            |
| string-keyed map                              | `llvm::StringMap`                        | String keys owned by the map; iteration order is not stable                                                  |
| `std::set` / `std::unordered_set`             | `llvm::SmallSet` / `llvm::DenseSet`      | Small or hash-friendly sets                                                                                  |
| `std::function` for immediate calls           | `llvm::function_ref`                     | Non-owning, synchronous callbacks only                                                                       |
| move-only stored callback                     | `llvm::unique_function`                  | The callback must outlive the call site                                                                      |
| `std::ifstream` whole-file read               | `llvm::MemoryBuffer`                     | File reads; check the returned `ErrorOr`                                                                     |
| `std::ofstream` file writes                   | `llvm::raw_fd_ostream`                   | File writes; check `has_error()`/`error()`                                                                   |
| `std::regex`                                  | `llvm::Regex`                            | Parsing; prefer a small hand-written parser on hot paths                                                     |
| filesystem queries                            | `llvm::sys::fs` / `llvm::sys::path`      | Filesystem access; `std::filesystem` remains acceptable where it is clearer or the target does not link LLVM |
| line-by-line reading                          | `llvm::line_iterator`                    | Line-oriented parsing                                                                                        |
| `std::optional`                               | `std::optional`                          | Prefer the standard type; `llvm::Optional` is obsolete and must not be added                                 |
| error-or-value                                | `llvm::ErrorOr<T>` / `llvm::Expected<T>` | LLVM APIs that already use them, or rich error propagation in LLVM-linked code                               |
| `std::thread`                                 | `llvm::thread`                           | Only when stack-size or LLVM's single-thread mode is required                                                |

`llvm::ArrayRef<T>` / `llvm::MutableArrayRef<T>` are preferred for read-only or mutable borrowed spans in LLVM-facing APIs. They are non-owning views; do not store them past the underlying storage lifetime.

LLVM ADT is not mandatory. Keep the standard type when it is clearer, when the container is large, or when the element count is unknown; measure before replacing.

### Correctness rules

1. **Lifetime first.** `StringRef`, `ArrayRef` and `function_ref` are non-owning. Never return them from a function whose storage dies at return, or store them in a long-lived object.
2. **Determinism.** `DenseMap`, `DenseSet`, `StringMap` and `SmallSet` do not promise stable iteration order. Sort keys before serializing, hashing or emitting artifacts. Never let hash-table order affect `.abix` output.
3. **Error handling.** `llvm::Expected<T>` must be checked. Do not ignore its error by discarding the value without `takeError()`/`consumeError()`. `ErrorOr<T>` is for `std::error_code`-style results.
4. **Stable wire formats, not LLVM in-memory layouts.** LLVM types may appear in public headers and APIs. `.abix`, `.semantic`, `.lowering` and metadata records must serialize stable primitive/ABI records; do not write LLVM object layouts, pointer values or version-specific LLVM types directly.
5. **Measure before replacing.** `SmallVector` and `DenseMap` are not automatic wins for every container. Keep the standard type when the code is clearer, the container is large, or the element count is unknown.
6. **Do not use `llvm::Optional`.** It is removed from modern LLVM. Use `std::optional`, `llvm::ErrorOr<T>` or `llvm::Expected<T>`.
7. **Prefer LLVM APIs when already in LLVM code.** If a function already returns `llvm::Expected` or takes `StringRef`, keep the LLVM type at that boundary instead of converting back and forth.

### Review checklist

- [ ] Does this target link the LLVM/Clang library it uses?
- [ ] Are LLVM types appropriate for this API, and kept out of serialized wire formats?
- [ ] Is every `StringRef`/`ArrayRef`/`function_ref` lifetime safe?
- [ ] Is any hash-table iteration order used for deterministic output?
- [ ] Is every `Expected` error checked?
- [ ] Was the replacement measured or justified, not applied mechanically?
- [ ] Is LLVM/Clang version compatibility considered for public APIs?

---

## Build and test expectations

- Targets that use LLVM link the **LLVM shared library** when the installed LLVM exposes it (`AMC_LLVM_DYNAMIC=ON`, the default). This includes providers and any core/codegen target that adopts LLVM. This keeps one copy of LLVM Support in `libLLVM` instead of embedding it in every target. When no dylib target is available (notably MSVC), configure falls back to static component libraries. Use `-DAMC_LLVM_DYNAMIC=OFF` to force the static fallback.
- The LLVM major version is a runtime dependency of any dynamically linked target; do not assume cross-major ABI compatibility.

- Keep `ctest` green; the boundary gates are `amc-boundary-contracts` and `amc-boundary-edges --strict`.
- Do not add a forbidden module edge without a staged `tools/module_boundary_allowlist.txt` entry naming the removal stage.
- Generated code templates live in `CodeIn/`; read them through the generated `SKL/AMC/Config.h` path rather than hard-coding source-relative paths.
- Prefer LLVM ADT/Support where it improves clarity, allocation or interoperability; standard C++17 types remain valid where they are clearer or a target does not link LLVM.
