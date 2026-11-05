# Dependency checkout

This directory holds the ABIX checkout that AMC's CMake fallback creates when no installed ABIX package can be found.

When `find_package(ABIX 0.3 CONFIG COMPONENTS runtime)` fails, the build uses CMake `FetchContent` to clone ABIX (including its MICS submodule) here:

```text
dependencies/
└── ABIX/          # https://github.com/Sukanle/ABIX.git (MICS in ABIX/MICS)
```

ABIX is configured as a runtime-only subproject. All other dependencies are not fetched: LLVM/Clang, fmt and toml++ must be installed by the user, and AMC uses whichever versions are available. The minimum versions recorded in `cmake/Dependencies.cmake` (`AMC_FMT_MIN_VERSION`, `AMC_TOMLPLUSPLUS_MIN_VERSION`) are enforced as lower bounds, so newer releases are accepted and used as-is.

Notes:

- This is **not** a source directory for AMC. Do not edit files here.
- These checkouts are **not tracked** by Git; only this README is tracked.
- There is no separate requirements list or maintenance workflow for this directory. Delete it at any time and CMake will re-create it on the next configure. An existing checkout is reused directly; passing a different `AMC_ABIX_REF` does not update it automatically, so delete `ABIX/` (or the whole directory) when you need a refresh.
- To use a specific ABIX revision (the default is the released `v0.3.0` branch/tag), pass `-DAMC_ABIX_REF=<ref>` when configuring. The MICS revision comes from ABIX's submodule pointer.
- Prefer installing ABIX and passing `-DABIX_DIR=<prefix>/lib/cmake/ABIX` when you want a reproducible build that does not download dependencies. The AMC CMake package export is only generated in this installed-package mode.
- On CI, cache this directory with `actions/cache` keyed by the requested ABIX revision.
