# Dependency checkout

This directory is used by AMC's CMake fallback when no installed `ABIX 0.3`
package can be found.

When `find_package(ABIX 0.3 CONFIG COMPONENTS runtime)` fails, the build uses
CMake `FetchContent` to clone ABIX and its MICS/HLMD dependencies here:

```text
dependencies/
├── ABIX/   # https://github.com/Sukanle/ABIX.git
└── MICS/   # https://github.com/Sukanle/MICS.git (includes HLMD)
```

Notes:

- This is **not** a source directory for AMC. Do not edit files here.
- These checkouts are **not tracked** by Git; only this README is tracked.
- There is no separate requirements list or maintenance workflow for this
  directory. Delete it at any time and CMake will re-create it on the next
  configure.
- To use a specific ABIX or MICS revision, pass `-DAMC_ABIX_REF=<ref>` or
  `-DAMC_MICS_REF=<ref>` when configuring.
- Prefer installing ABIX and passing `-DABIX_DIR=<prefix>/lib/cmake/ABIX` when
  you want a reproducible build that does not download dependencies.
- On CI, cache this directory with `actions/cache` keyed by the requested refs.
