# AMC Module Boundaries (A0)

Status: **A0 boundary freeze**. This document defines the contracts between the
AMC frontend, IR, pass pipeline, backend, compilation pipeline and external
interfaces. It does not move existing code yet; A1 starts the physical split.

## Goals

- Make ABIX IR the center of the toolchain instead of any one source language.
- Make the dependency direction explicit and mechanically checkable.
- Keep every existing interop and conformance test green during the migration.

## Modules

| Module | Responsibility | Owns |
| --- | --- | --- |
| Frontend | Understand a source language and emit IR | language parsing, AST extraction, source semantics |
| IR | Express language-independent interface semantics | types, identities, ABI mapping, ownership, lifetime, errors, versions, capabilities |
| Pass | Validate and transform IR | normalization, validation, compatibility, lowering |
| Backend | Emit target files from validated IR | C++/Rust/Zig/Protobuf/gRPC generators |
| Compilation | Orchestrate one interface compilation | context, session, pipeline, cache, build options |
| Interfaces | Expose compilation to users and agents | CLI, MCP, LSP, SDK |

Support modules: `Artifact`, `Diagnostics`, `Platform`, `Serialization`,
`Query`, and `Support`.

## Dependency Rules

Allowed:

```text
Frontend ──► IR
Pass     ──► IR + Diagnostics
Backend  ──► IR + Diagnostics
Compilation ──► Frontend + Pass + Backend + IR
Interfaces  ──► Compilation
```

Forbidden:

- IR must not include a source-language parser, Clang AST, or a backend.
- A frontend must not include a backend or call a backend directly.
- A backend must not include Clang AST or parse source languages.
- CLI/MCP/LSP/SDK must not implement configure/frontend/pass/backend logic.
- A pass must not depend on a specific backend.
- No module may bypass diagnostics for a recoverable failure.

## Data Flow and Ownership

```text
Request
  │
  ▼
Frontend ──► IR (owned by Compilation)
  │
  ▼
Pass pipeline (mutates the working IR)
  │
  ▼
Backend (borrows a validated IR, emits artifacts)
  │
  ▼
Result + Diagnostics
```

- The compilation session owns the IR and the working copies.
- A pass may mutate the IR but must leave it valid when it returns `true`.
- A backend receives `const IR*` and must not mutate it.
- Diagnostics are accumulated in one list and are the only recoverable error
  channel across module boundaries.
- No C++ exception may cross a module boundary.

## Contract Headers

A0 freezes these headers:

| Header | Contract |
| --- | --- |
| `include/SKL/AMC/IR/Module.h` | `amc::ir::Module`, schema version |
| `include/SKL/AMC/Diagnostics/Diagnostics.h` | `Diagnostic`, `Severity`, `DiagnosticList` |
| `include/SKL/AMC/Frontend/Frontend.h` | `Request`, `Result`, `Frontend` |
| `include/SKL/AMC/Pass/Pass.h` | `Context`, `Pass`, `Pipeline` |
| `include/SKL/AMC/Backend/Backend.h` | `Artifact`, `Request`, `Result`, `Backend` |
| `include/SKL/AMC/Compilation/Compilation.h` | `Request`, `Result`, `Compiler` |

The A0 `ir::Module` aggregates the existing ABI module and semantic model so
the boundary can be frozen before A2 merges their data layout.

## IR Responsibilities

The IR must distinguish:

- logical type (for example extended floating point);
- physical representation (IEEE binary64, x87 extended, PPC double-double);
- ABI mapping (size, alignment, register class, calling convention).

It also carries type/interface/method identity, ownership, lifetime, error
contracts, versions, capabilities and metadata. The schema version is
independent from the runtime ABI version.

## A4 Progress

- `ExportSpec`, `parse_exports` and `apply_target_and_layout` moved to
  `Compilation/Config` (`amc-compilation`)
- provider manifest/IPC/dispatch/generate moved to `Compilation/Provider`
- the build pipeline moved to `Compilation/Build`; `.abix.meta` emission moved
  to `Compilation/Meta`
- CLI shared formatting/diagnostics helpers moved to `CLI/Support`; `CLI.h` no
  longer drags the IR into the interface layer
- zero unlisted boundary edges and an empty allowlist

## Compatibility Forwarders

A1 used explicitly marked forwarders while providers migrated. All providers now
include their target headers directly, so **no compatibility forwarders remain**.

Any future forwarder must carry the `AMC_COMPAT_FORWARD` marker, name its target
and be removed once `tools/check_module_boundaries.py` reports no include sites.

Run:

```bash
python3 tools/check_module_boundaries.py            # report-only
python3 tools/check_module_boundaries.py --strict   # fail on unlisted edges
python3 tools/check_module_boundaries.py --contracts-only
```

Temporary exceptions live in `tools/module_boundary_allowlist.txt`, one
`file|from|to|until|reason` entry per line. The `until` column names the stage
that removes the edge; stale entries are reported so the allowlist does not
rot. `--strict` accepts allowlisted edges and fails only on unlisted ones.

Current exceptions: none. The allowlist file is intentionally empty.

CTest runs both `--contracts-only` and `--strict` as `amc-boundary-contracts`
and `amc-boundary-edges`.

## Current → Target Mapping

| Current | Target |
| --- | --- |
| `Core/Core.h` (`AbiModule`) | `IR/` ABI view |
| `Semantic/*` | `IR/` semantic view |
| `Lang/C++/Frontend.cpp` | `Frontend/Cpp/` |
| `Lang/C++/Backend.cpp` | `Backend/Cpp/` |
| `Lang/Rust/*` | `Frontend/Rust/` + `Backend/Rust/` |
| `Lang/Zig/*` | `Backend/Zig/` |
| `CLI/*` | `Interfaces/CLI/` + `Compilation/` |
| `Tool/MCP/*` | `Interfaces/MCP/` |
| `Core/Verify.cpp` | `Pass/Compatibility` |
| `Core/Cache.cpp` | `Compilation/Cache` |
| `Core/ELF.cpp`, `Core/ELF.h` | `Platform/ELF` |
| `Core/Metadata.cpp` | `Artifact/` / `Serialization/` |
| `Core/Query.cpp`, `Core/Context.cpp` | `Query/` |
| `Tool/Dump/*` | `Interfaces/CLI` or `Query/` |

## A1 Progress

- `Lang/C++/Frontend.cpp` → `Frontend/Cpp/Frontend.cpp`
- `Lang/C++/Backend.cpp` → `Backend/Cpp/Backend.cpp`
- `Lang/Rust/Frontend.cpp` → `Frontend/Rust/Frontend.cpp`
- `Lang/Rust/Backend.cpp` → `Backend/Rust/Backend.cpp`
- `Lang/Zig/Backend.cpp` → `Backend/Zig/Backend.cpp`
- all providers include their target headers directly; compatibility forwarders
  have been removed
- the scanner reports zero unlisted forbidden edges; three CLI -> IR edges
  remain as allowlisted A4 exceptions
- `amc-boundary-contracts` and `amc-boundary-edges` run in CTest

## Migration Rules

1. Do not move a directory and change its behavior in the same step.
2. Freeze a contract header before moving its first implementation.
3. Keep compatibility forwarding headers while callers migrate.
4. Every migration step must keep `ctest` and interop tests green.
5. A module may only gain a dependency that the rules above allow.
6. New code must use the A0 contract headers instead of reaching across modules.

## A0 Acceptance

- [x] Six module responsibilities and support modules documented.
- [x] Dependency direction and forbidden edges documented.
- [x] Data flow, ownership and diagnostic rules documented.
- [x] Boundary headers exist and compile.
- [x] `amc-boundary-test` covers the contract types and pass ordering.
- [x] A report-only dependency scanner and an A0 contract-header gate exist.
- [x] `--strict` edge gate passes with documented A4 exceptions.
- [ ] Current includes are migrated behind the contract headers in A1.

## A1 Entry Criteria

- The contract headers are accepted as the public module surface.
- The Frontend/Backend split has a file-level migration map.
- A dependency check runs in report-only mode against the current tree (done).
- No A0 contract header includes Clang AST, a backend, or an interface layer.
