# Capability Discovery

AMC records capabilities in the artifact and in the generated language
bindings. Discovery never guesses at private C++ details: only entities that a
frontend could prove and record are exposed.

## Source is available

An `[[expose]]` declaration asks the C++ frontend to generate C thunks for an
opaque handle. The frontend records:

- the handle TypeID and its `create` / `destroy` thunks;
- every method thunk signature;
- the named interface contract (`[[interface]]` / `[[implements]]`) when present.

`amc build` writes this information into:

- `<artifact>.abix` — ABI entities, interface contracts, error sets;
- `<artifact>.semantic` — source-level exposed-handle facts;
- `<artifact>.lowering` — the validated exposed/interface/ownership contract
  consumed by backends.

The generated bindings expose typed values for the same facts:

| Target | Exposed handle | Interface contract |
| --- | --- | --- |
| Rust | `Owned<T>` methods from the lowering contract | `capabilities` module with IDs, versions, slots and signatures |
| C++ | move-only `<Type>Handle` | `k<Name>Interface`, `<Name>Traits`, proxy class |
| Zig | `<Type>Handle` with `create` / `deinit` / methods | `CapabilityContract` and `CAPABILITIES` |

## Only a compiled module is available

When only a shared library is available, AMC can discover:

- the exported symbol table;
- the embedded ABIX Metadata Region (`amc metadata --from-elf`);
- explicitly registered capability manifests / interfaces.

It cannot recover:

- inline or fully devirtualized functions;
- private members or private methods;
- local symbols stripped from the binary;
- arbitrary C++ semantics that were not recorded by a frontend.

## Failure policy

A lowering sidecar and artifact must come from the same build. If a lowered
entity references a TypeID or signature that is absent from the artifact,
`amc generate` fails with a diagnostic instead of projecting stale data:

```text
lowering sidecar references an ABI type missing from the artifact: ...
```

The same rule applies to exposed handles, interface methods, ownership and
error contracts: an unprovable capability is reported, not guessed.
