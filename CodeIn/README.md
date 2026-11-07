# CodeIn templates

`CodeIn/` stores static source templates that AMC copies into generated output. Keeping them as real files makes them editable, reviewable and syntax-highlighted instead of embedding large raw strings in C++.

Current layout:

```text
CodeIn/
├── Cpp/
│   └── Prologue.hpp  # generated C++ preamble, aliases, Result, map types
└── Rust/
    └── MetaData.rs   # zero-allocation Rust metadata-region reader
```

## Path resolution

The build generates `SKL/AMC/Config.h` from `cmake/Config.h.in`:

- `AMC_CODEIN_DIR` — absolute build-tree path to this directory.
- `AMC_CODEIN_INSTALL_SUBDIR` — install-relative path (`<datadir>/AMC/CodeIn`).

At runtime a backend uses the shared loader in `include/SKL/AMC/Backend/CodeIn.h`. It tries, in order:

1. `AMC_CODEIN_DIR/<relative path>`
2. `<executable>/../<AMC_CODEIN_INSTALL_SUBDIR>/<relative path>`
3. `<executable>/CodeIn/<relative path>`

The second and third candidates keep installed providers working after `cmake --install` moves them out of the source tree. Loaded assets are cached for the life of the process.

C++ / Rust / Zig backends should use `amc::backend::read_codein_text` instead of duplicating path lookup. Use the error-returning overload when a missing asset should become a normal backend error; use the string overload when the caller treats it as fatal.

## Shared codegen helpers

Language-neutral spelling helpers live in `include/SKL/AMC/Backend/Codegen.h`:

- `hex_u64` / `hex_u64_ull` — stable hexadecimal literals.
- `sanitize_identifier` — `::` and non-identifier characters become `_`.
- `escape_string_literal` — C-family string literal escaping.

Backends should call these instead of reimplementing the same loops.

## Adding a template

1. Put the file under `CodeIn/<language>/<name>.<ext>`.
2. Read it through `amc::backend::read_codein_text`, not a hard-coded path.
3. Add a runtime test that generates the target file and checks the template content is present.
4. Keep dynamic loops and conditionals in the backend emitter; CodeIn is for stable skeletons and runtime fragments, not a general template language.
