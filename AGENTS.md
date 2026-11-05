# AGENTS.md — AMC Agent Constraints

AMC is an **interface compiler**: it understands source-language interfaces,
lowers them into ABIX IR, validates the result and projects it through
language backends. It is not a program compiler, a universal object model or a
runtime replacement.

Read `docs/architecture/module-boundaries.md` before changing module
dependencies. The dependency direction is:

```text
Frontend -> IR -> Pass -> Backend
                 \-> Compilation -> Interfaces
```

Do not introduce a dependency that points back from IR/Pass/Backend to a
specific frontend, a CLI command or a Clang AST.

---

## LLVM ADT / Support usage

AMC is built against LLVM/Clang, and LLVM's `llvm/ADT` and `llvm/Support`
contain production-tested replacements for parts of the C++ standard library.
Prefer them in **LLVM-linked code** when they improve allocation behaviour,
lifetime clarity or LLVM API interoperability.

### Scope restriction

All AMC language providers link LLVM Support intentionally:

- `amc-cpp` / `Frontend/Cpp` / `Backend/Cpp`
- `amc-rust` / `Frontend/Rust` / `Backend/Rust`
- `amc-zig` / `Backend/Zig`
- `amc-cancel-test-provider` and LLVM-aware tests

Use LLVM ADT/Support in provider implementation code, especially for
filesystem access, file I/O, line iteration and regex-like parsing. Avoid
reimplementing these facilities with `std::filesystem`, `std::fstream` or
`std::regex` inside a provider.

Do **not** add LLVM includes to:

- public A0/A4 contract headers (`IR`, `Frontend`, `Pass`, `Backend`,
  `Compilation`, `Diagnostics`)
- stable IR/artifact records or headers intended for external consumers
- `Core` libraries that are linked by non-provider consumers/tests

Keep LLVM as an implementation detail of provider executables. Public headers
stay C++17-standard-library-only (or standard types already used by the API).

### Preferred replacements

| Standard library | LLVM facility | Use when |
| --- | --- | --- |
| `std::vector` | `llvm::SmallVector<T, N>` | Small, frequently created containers; choose `N` from measured usage, not by habit |
| `const char*` / `std::string_view` parameters | `llvm::StringRef` | Non-owning read-only strings; never store beyond the source lifetime |
| `std::unordered_map` | `llvm::DenseMap` | Pointer/integer keys and cache-friendly iteration |
| string-keyed map | `llvm::StringMap` | String keys owned by the map; iteration order is not stable |
| `std::set` / `std::unordered_set` | `llvm::SmallSet` / `llvm::DenseSet` | Small or hash-friendly sets |
| `std::function` for immediate calls | `llvm::function_ref` | Non-owning, synchronous callbacks only |
| move-only stored callback | `llvm::unique_function` | The callback must outlive the call site |
| `std::ifstream` whole-file read | `llvm::MemoryBuffer` | Provider file reads; check the returned `ErrorOr` |
| `std::ofstream` file writes | `llvm::raw_fd_ostream` | Provider file writes; check `has_error()`/`error()` |
| `std::regex` | `llvm::Regex` | Provider parsing only; prefer a small hand-written parser on hot paths |
| filesystem queries | `llvm::sys::fs` / `llvm::sys::path` | Provider filesystem access; do not add `std::filesystem` to providers |
| line-by-line reading | `llvm::line_iterator` | Provider line-oriented parsing |
| `std::optional` | `std::optional` | Prefer the standard type; `llvm::Optional` is obsolete and must not be added |
| error-or-value | `llvm::ErrorOr<T>` / `llvm::Expected<T>` | LLVM APIs that already use them, or rich error propagation in LLVM-linked code |
| `std::thread` | `llvm::thread` | Only when stack-size or LLVM's single-thread mode is required |

`llvm::ArrayRef<T>` / `llvm::MutableArrayRef<T>` are preferred for read-only or
mutable borrowed spans in LLVM-facing APIs. They are non-owning views; do not
store them past the underlying storage lifetime.

### Correctness rules

1. **Lifetime first.** `StringRef`, `ArrayRef` and `function_ref` are
   non-owning. Never return them from a function whose storage dies at return,
   or store them in a long-lived object.
2. **Determinism.** `DenseMap`, `DenseSet`, `StringMap` and `SmallSet` do not
   promise stable iteration order. Sort keys before serializing, hashing or
   emitting artifacts. Never let hash-table order affect `.abix` output.
3. **Error handling.** `llvm::Expected<T>` must be checked. Do not ignore its
   error by discarding the value without `takeError()`/`consumeError()`.
   `ErrorOr<T>` is for `std::error_code`-style results.
4. **No LLVM in stable interfaces.** LLVM types must not leak into A0/A4
   contract headers, artifact records or the Rust/Zig providers.
5. **Measure before replacing.** `SmallVector` and `DenseMap` are not automatic
   wins for every container. Keep the standard type when the code is clearer,
   the container is large, or the element count is unknown.
6. **Do not use `llvm::Optional`.** It is removed from modern LLVM. Use
   `std::optional`, `llvm::ErrorOr<T>` or `llvm::Expected<T>`.
7. **Prefer LLVM APIs when already in LLVM code.** If a function already returns
   `llvm::Expected` or takes `StringRef`, keep the LLVM type at that boundary
   instead of converting back and forth.

### Review checklist

- [ ] Does this target link LLVM?
- [ ] Is the LLVM type kept out of public/stable headers and non-LLVM providers?
- [ ] Is every `StringRef`/`ArrayRef`/`function_ref` lifetime safe?
- [ ] Is any hash-table iteration order used for deterministic output?
- [ ] Is every `Expected` error checked?
- [ ] Was the replacement measured or justified, not applied mechanically?

---

## Build and test expectations

- Keep `ctest` green; the boundary gates are `amc-boundary-contracts` and
  `amc-boundary-edges --strict`.
- Do not add a forbidden module edge without a staged
  `tools/module_boundary_allowlist.txt` entry naming the removal stage.
- Generated code templates live in `CodeIn/`; read them through the generated
  `SKL/AMC/Config.h` path rather than hard-coding source-relative paths.
- Prefer standard C++17 in public headers; LLVM is an implementation detail.
