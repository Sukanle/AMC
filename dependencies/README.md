# Dependency checkout

This directory is used by AMC's CMake fallback when a required dependency is
not available as an installed package.

The build uses CMake `FetchContent` to place dependencies here:

```text
dependencies/
├── ABIX/          # https://github.com/Sukanle/ABIX.git
├── MICS/          # https://github.com/Sukanle/MICS.git (includes HLMD)
├── fmt/           # pinned fmt tarball
└── tomlplusplus/  # pinned toml++ tarball
```

`ABIX` and `MICS` are fetched when `find_package(ABIX 0.3 CONFIG COMPONENTS
runtime)` fails. `fmt` and `tomlplusplus` are fetched when no installed package
meets the minimum versions recorded in `cmake/AMCDependencies.cmake`
(`AMC_FMT_VERSION`, `AMC_TOMLPLUSPLUS_VERSION`). This avoids distribution
packages whose API is too old, such as Ubuntu 24.04's fmt 9, which lacks
`fmt::println`.

Notes:

- This is **not** a source directory for AMC. Do not edit files here.
- These checkouts are **not tracked** by Git; only this README is tracked.
- There is no separate requirements list or maintenance workflow for this
  directory. Delete it at any time and CMake will re-create it on the next
  configure.
- To use a specific ABIX, MICS, fmt or toml++ revision, pass
  `-DAMC_ABIX_REF=<ref>`, `-DAMC_MICS_REF=<ref>`,
  `-DAMC_FMT_VERSION=<version>` or `-DAMC_TOMLPLUSPLUS_VERSION=<version>` when
  configuring.
- Prefer installing ABIX, fmt and toml++ and passing their `*_DIR` values when
  you want a reproducible build that does not download dependencies. The AMC
  CMake package export is only generated in this installed-package mode.
- On CI, cache this directory with `actions/cache` keyed by the requested refs
  and pinned versions.
