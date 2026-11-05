# CodeIn templates

`CodeIn/` stores static source templates that AMC copies into generated output.
Keeping them as real files makes them editable, reviewable and syntax-highlighted
instead of embedding large raw strings in C++.

Current layout:

```text
CodeIn/
└── Rust/
    └── MetaData.rs   # zero-allocation Rust metadata-region reader
```

## Path resolution

The build generates `SKL/AMC/Config.h` from `cmake/Config.h.in`:

- `AMC_CODEIN_DIR` — absolute build-tree path to this directory.
- `AMC_CODEIN_INSTALL_SUBDIR` — install-relative path
  (`<datadir>/AMC/CodeIn`).

At runtime a provider tries, in order:

1. `AMC_CODEIN_DIR/<relative path>`
2. `<executable>/../<AMC_CODEIN_INSTALL_SUBDIR>/<relative path>`
3. `<executable>/CodeIn/<relative path>`

The second and third candidates keep installed providers working after
`cmake --install` moves them out of the source tree.

## Adding a template

1. Put the file under `CodeIn/<language>/<name>.<ext>`.
2. Read it through the build-generated `Config.h` path, not a hard-coded
   relative path.
3. Add a runtime test that generates the target file and checks the template
   content is present.
