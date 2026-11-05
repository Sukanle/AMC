# AMC Module Boundaries (A0)

Status: **A0 frozen; A1/A4/A5 landed, A2/A3/A6 in progress**. This document defines
the contracts between the AMC frontend, IR, pass pipeline, backend, compilation
pipeline and external interfaces. The remaining work is boundary consolidation
and physical module extraction, not a new architecture.

## Goals

- Make ABIX IR the center of the toolchain instead of any one source language.
- Make the dependency direction explicit and mechanically checkable.
- Keep every existing interop and conformance test green during the migration.

## AMC Compiler Boundary

AMC is a **pre-compilation** semantic analysis and code generation toolchain.
It parses and analyzes source-language semantics, constructs an IR, performs ABI
lowering, and generates ABI contracts and source artifacts.

AMC does **not** implement language compilation, machine-code generation,
optimization, or linking. Those stay with the user's toolchain. AMC may generate
helper translation units, call a compiler frontend, or request template
instantiation facts, but it must not become a compiler driver, build system or
optimizer.

Consequences:

- `.abix` / `.semantic` / `.lowering` are contracts and generation inputs, not
  object files or machine code.
- The Compilation module orchestrates frontend/pass/backend work; it does not
  replace the user's compiler or linker.
- Generated bindings stop at source-level artifacts and ABI declarations.

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

## M4 Progress

- `IR/Lowering.h` defines `AbiTypeId` / `AbiFunctionId`, lowered type/function/
  ownership/template records and the `LoweringResult` contract
- `Pass/Lowering` links semantic types, functions, inheritance, fields,
  ownership and materialized templates to their ABI counterparts
- `ir::Module` carries the lowering result; `amc-lowering-test` covers mapping,
  direct `abi_type` / `abi_signature` links, missing/ambiguous entities, strict
  mode and the lowering JSON round trip
- the C++ frontend synchronizes every ABI type/function into the semantic model
  with a direct link; `Semantic/Sync` is reusable by other frontends
- `Semantic/Serialize` reads and writes the sidecar, including records,
  functions, ownership and the ABI links
- `Compilation/Build` reads the sidecar, completes missing links, runs
  `pass::lower` and writes `<artifact>.lowering` after symbol projection
- backends load the optional lowering sidecar and consume lowered type/function
  indexes and ownership contracts instead of re-deriving them by name

## M5 Progress

- `[[expose]]` records an explicit `semantic::ExposedType` / `ExposedMethod`
  contract (handle type, create/destroy thunk, method thunk signatures); the
  semantic sidecar reader/writer round-trips it
- `Pass/Lowering` lowers the exposed contract into `LoweredExposedType`, and
  `<artifact>.lowering` carries it to every backend
- Rust projects `Owned<T>` with typed method wrappers from the lowered contract
- C++ projects a move-only RAII `<Type>Handle` with a factory, typed methods and
  a C-linkage declaration block for the thunks
- Zig projects a `<Type>Handle` with `create` / `deinit` / typed methods and now
  emits an opaque blob for `type_opaque` records instead of leaking private
  fields
- Rust, C++ and Zig project declared error boundaries as typed results
  (`Result<T, E>` / `AmcResult(T, E)`) instead of a bare status code
- Rust and Zig also emit typed interface capability contracts (identity,
  version, slots, signatures); C++ already emits `k<Name>Interface` /
  `<Name>Traits`
- Rust and Zig emit typed interface proxies that validate interface id,
  version, slot and signature at bind time; C++ uses the ABIX proxy runtime
- generated proxies carry an `InterfaceLease`/`lease` value whose drop/deinit
  releases the caller-owned module lease
- the interop suite binds the same handle-shaped interface through C++, Rust
  and Zig against the same native library and rejects stale version/signature
  contracts
- the registry consumer covers module lease / begin_unload / wait_for_idle
  lifecycle, and `tests/interop/m7/` is an independent same-library/same-contract
  L3 fixture with target, stale-lowering, cross-artifact interface-version and
  compile-time layout rejection
- `Verify` compares interface contracts, so an interface version/slot/signature
  change is reported as contract drift across artifacts
- generation rejects a lowering sidecar that references a TypeID or signature
  absent from the artifact, instead of projecting stale data
- `tests/interop/expose_test.py` drives the same exposed contract and typed
  Result wrappers through Rust, C++ and Zig runtime consumers; all thunk names
  are resolved from signatures, never parsed
- capability discovery scope and the stale-sidecar failure policy are documented
  in `docs/amc/capability-discovery.md`

## M6 Progress

- EPCD 0.1 is a required Core dependency; the build accepts an installed
  package or fetches/uses a repository-local checkout, and links
  `EPCD::Interface` for registration when available
- `SKL/AMC/Error.h` defines the stable `AMCErr` static domain, `from_std_errc`,
  `error_message`, and idempotent `register_errors`
- `amc::Error` carries `skl::epcd::ErrorCode`; `abix.error/1` adds optional
  `domain` / `value` while keeping the existing fields stable
- `[[error]]` v2 requires a domain, maps `GenericCondition`, and supports
  `return_field`; `[[error-set]]` / `[[error-set.code]]` validate, serialize and
  round-trip stable error-code tables
- Rust, C++ and Zig project named error sets with stable numeric values, names
  and generic conditions; typed Result wrappers use the named set when present
  and preserve unknown codes numerically
- the interop suite checks the named error-set projection and typed Result
  behavior across Rust, C++ and Zig

## Current → Target Mapping

| Item | Target | Status |
| --- | --- | --- |
| `Core/Core.h` (`AbiModule`) | `IR/` ABI view | remaining |
| `Semantic/*` | `IR/` semantic view | remaining |
| `Lang/C++/Frontend.cpp` | `Frontend/Cpp/` | moved |
| `Lang/C++/Backend.cpp` | `Backend/Cpp/` | moved |
| `Lang/Rust/*` | `Frontend/Rust/` + `Backend/Rust/` | moved |
| `Lang/Zig/*` | `Backend/Zig/` | moved |
| `CLI/*` | `Interfaces/CLI/` + `Compilation/` | remaining |
| `Tool/MCP/*` | `Interfaces/MCP/` | remaining |
| `Core/Verify.*` | `IR/Verify` (types/policy) + `Pass/Compatibility` (pass) | moved (A5.7) |
| `Core/Error.*` | `Diagnostics/Error` | moved (A5.1) |
| `Core/Cache.*` | `Compilation/Cache` | moved (A5.2) |
| `Core/Metadata.*`, `Core/Materialize.*` | `Artifact/` | moved (A5.3) |
| `Core/ELF.*`, `Core/Symbol.*` | `Platform/` | moved (A5.4) |
| `Core/Query.*`, `Core/Context.*` | `Query/` | moved (A5.5) |
| `Core/Json.*` | `Support/Json` | moved (A5.6) |
| `Core/Lua.*` | `Backend/Lua/Lua` | moved (A6) |
| `Core/Condition.*` | `IR/Condition` | moved (A5.8) |
| `Tool/Dump/*` | `Interfaces/CLI` or `Query/` | remaining |

## A2/A3 Progress

- `IR/Artifact` loads `.abix` + optional `.semantic` / `.lowering` into one
  `ir::Module`; backends use that boundary instead of repeating sidecar paths
- `Pass/Artifact` validates IR schema and sidecar metadata; `Pass/Semantic`
  validates semantic references; `Pass/Verify` owns ABI and read-only
  lowering-reference validation; `Pass/Compatibility` wraps contract drift;
  `Pass/TypeMapping` owns mapping reports; `Pass/Pipeline` composes
  artifact/semantic/ABI verification + lowering + lowering verification with
  selectable pass/strict options
- `Compilation/Build` runs the standard pass pipeline; `amc verify`, `amc diff`
  and `amc compatibility` use Pass entry points
- A3.5 policy facades: `IR/Compatibility` and `IR/TypeMapping` name the
  comparison/mapping policies; `Pass/*` adds diagnostics/strict behaviour and
  `Query/Compatibility` consumes the same policy without a Query -> Pass edge
- `Diagnostics` owns aggregation (`Summary`), text rendering and the
  `amc.diagnostics/1` JSON schema; `Diagnostics/Verify` converts
  `VerifyResult` drift into the shared model, and `amc verify --format
  diagnostics-json` emits it
- A2/A3.5 close-out: the remaining work is the A5 physical split, not policy
  design; `amc-pass-perf-test` (5k default, 20k with
  `AMC_PASS_PERF_LARGE=1`) records the pass baseline for that move

## A5 Progress

- A5.1 Diagnostics: `Core/Error.*` → `Diagnostics/Error.*`; AMC error
  presentation now lives with the diagnostics schema, while EPCD remains the
  error-identity source
- A5.2 Cache: `Core/Cache.*` → `Compilation/Cache.*`; the implementation moved
  from `libabix-tools` to `amc-compilation`, and `amc-core-test` links
  `amc-compilation` for the cache cases
- A5.3 Artifact: `Core/Metadata.*` and `Core/Materialize.*` →
  `Artifact/Metadata.*` and `Artifact/Materialize.*`; `libabix-metadata` is
  the artifact library and keeps the region/materialization implementation
- A5.4 Platform: `Core/ELF.*` and `Core/Symbol.*` → `Platform/ELF.*` and
  `Platform/Symbol.*`; new `amc-platform` static library (`AMC::platform`)
  owns ELF/Mach-O/PE section access and build-id/symbol-store helpers
- boundary tooling now maps `Artifact`, `Platform`, `Query`, `Interfaces` and
  `Support` includes; Backend/Compilation/Query may consume Artifact,
  Query/Artifact may consume Platform, and no forwarders were added
- A5.5 Query: `Core/Query.*` and `Core/Context.*` → `Query/Query.*` and
  `Query/Context.*`; unused Query/Context includes were pruned from the
  CLI compilation commands, so boundary edges only exist where they are used
- A5.6 Support: `Core/Json.*` → `Support/Json.*`; the JSON wrapper stays in
  `libabix-format`, while the boundary scanner now recognizes `Support/`
  source paths and includes
- A5.7 Verify: `Core/Verify.*` → `IR/Verify.*`; `VerifyResult`/`ChangeKind`
  stay in the IR policy layer, Pass/Compatibility continues to add diagnostics
- A5.8 Condition: `Core/Condition.*` → `IR/Condition.*`, keeping the frozen
  error-boundary enum next to the ABI model
- remaining Core leftovers are the ABI model itself (`Core.h`/`Core.cpp`),
  `Adapter`, and `Lua`; they are outside the A5 infrastructure slice

## A6 Progress

- A6.0/A6.4: `Aue/README.md` records the Lua VM / ABIX Contract / Native
  Execution layering, the L0/L1/L2 downgrade rule, and the
  conformance/benchmark entry points
- A6.1: L1 pre-builds its name -> entry index once at registration; cold
  dispatch is a single table lookup instead of a linear contract scan
- A6.2: the Lua generator emits record/field descriptors; record metatables
  provide direct `__index`/`__newindex` access with explicit ABI width/kind
  mapping and reject unsupported writes
- A6.3: type metatables install a default `__gc` hook;
  `register_type_destroy` overrides it with a native destructor. The demo
  `Counter` maintains a live count, and conformance drops a temporary object
  and runs `collectgarbage` to prove the destructor runs before release
- `Core/Lua.*` moved to `Backend/Lua/Lua.*`; the generator now lives in the
  Backend module and adds no new boundary edge
- A6.4: `Aue/standalone/CMakeLists.txt` provides a driver-independent
  conformance/benchmark entry point. It installs `bin/abix-conformance`,
  `bin/abix-bench` and `share/aue/*`, and `cpack` produces a versioned
  `Aue-<version>-<system>.tar.gz`; the install was validated against an
  installed ABIX runtime
- A6.5: `-DAUE_ENABLE_L2=ON` builds the opt-in L2 native-dispatch prototype.
  It uses a C-side open-addressed contract index without memoization; it is a
  research step, not a Lua VM patch. Default builds remain L0/L1
- A6.6: field descriptors now distinguish scalar, pointer, record, array and
  opaque fields. Record/array fields return non-owning views that retain their
  parent through Lua uservalue 1; array element access uses `type_index` or
  `element_kind`/`element_size`, and unsupported writes are rejected

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
