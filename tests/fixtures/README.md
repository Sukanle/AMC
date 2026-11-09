# Test fixtures

Fixtures are grouped by source language and theme instead of living in one flat directory. A fixture's `.abic.toml` and its source/header stay in the same directory, so the relative `files` entries inside the config remain valid when a test copies the pair into its work directory.

```text
tests/fixtures/
├── cpp/
│   ├── basics/       # amc_test, amc_m8 and their headers
│   ├── templates/    # templates, inheritance and overload helpers
│   └── maps/         # map v1/v2 and conversion fixtures
└── rust/
    ├── demo/         # the general Rust ABI fixture
    ├── layouts/      # c_long, packed(N) and union layout probes
    ├── semantics/    # helpers, traits/impls and the semantic schema document
    ├── handoff/      # [[rust.expose]], [[owned]], [[contract]] and [[error]]
    ├── containers/   # String/Vec/Option/Box contract fixtures
    └── workspace/    # real Cargo workspace facts (members/dependencies/target)
```

Guidelines for new fixtures:

- Put a config and the source/header it imports in the same directory.
- Name paired files consistently (`<name>.abic.toml`, `<name>.rs` / `<name>.hpp`).
- Add a `README.md` only when a fixture needs non-obvious setup.
- Prefer a new theme directory over adding another top-level file.
