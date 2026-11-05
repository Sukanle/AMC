# AMC Benchmarks

<p align="center">
  English · <a href="README_ZH.md">中文</a>
</p>

This section records AMC's performance baselines, scaling behaviour and regression gates. It is evidence for the optimization plan, not a marketing page: every number below comes from a Release build, a pinned P-core set and Google Benchmark JSON.

> Baseline snapshot: **2026-11-06**, after the C++ frontend/backend split, shared CodeIn/Codegen helpers and the OPT-4/OPT-6/OPT-7 performance work.

## What is measured

| Suite     | Executable                         | What it answers                                                        |
| --------- | ---------------------------------- | ---------------------------------------------------------------------- |
| Pass      | `amc-pass-perf-bench`              | semantic/ABI verification, lowering, compatibility, full pipeline      |
| Artifact  | `amc-artifact-perf-bench`          | `.abix` read/write, sidecar load cost, metadata, materialization       |
| Cache     | `amc-cache-perf-bench`             | cold store, hit/miss, dependency invalidation, concurrent lookup       |
| Query     | `amc-query-perf-bench`             | type/function lookup, text/JSON rendering, LLM context                 |
| Multifile | `amc-multifile-perf-bench`         | in-process read-only artifact load with a thread pool (reference only) |
| External  | `tools/run_multiprocess_bench.py`  | build-system model: N independent `amc` processes                      |
| Stress    | `tools/stress_concurrent_build.py` | concurrent `amc build` with one shared cache                           |

## Method

- Release build, Google Benchmark, `--benchmark_repetitions=3`, `--benchmark_min_time=0.2s`.
- CPU affinity restricted to the P-core set (`tools/run_bench.py --pin p`).
- Values are **wall time** (`real_time`). `cpu_time` is not used for multi-threaded or multi-process cases because it only counts the calling thread.
- Inputs are generated deterministically by each fixture. Page cache is warm; this measures AMC, not cold disk I/O.
- `ctest -L perf` runs a short 0.05s smoke version of every suite.
- The pass/query/artifact tables are a point-in-time snapshot; absolute values will drift with compiler, CPU and governor.

Reproduce:

```bash
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -DAMC_BUILD_BENCHMARKS=ON
cmake --build build/Release --parallel

python tools/run_bench.py --pin p --repetitions 5 --out build/bench
python tools/run_multiprocess_bench.py \
  --artifact build/Release/Aue/generated/build/build/counter.abix \
  --instances 1 2 4 8 16
python tools/stress_concurrent_build.py \
  --config Aue/Counter.abic.toml --instances 8 --cold-rounds 2 --warm-rounds 4
```

## Pass pipeline

5k / 20k entities, P-core, mean wall time:

| Benchmark      |       5k |       20k | 4x input |
| -------------- | -------: | --------: | -------: |
| SemanticVerify | 0.724 ms |  3.158 ms |    4.36x |
| AbiVerify      | 1.260 ms |  5.581 ms |    4.43x |
| Lowering       | 1.995 ms | 10.080 ms |    5.05x |
| LoweringVerify | 2.869 ms | 13.296 ms |    4.63x |
| FullPipeline   | 4.698 ms | 21.150 ms |    4.50x |
| Compatibility  | 3.576 ms | 15.518 ms |    4.34x |

Interpretation:

- The former O(n²) paths are gone. A 4x input increase now costs 4.3–5.1x.
- Lowering is the steepest stage (5.05x) but still inside the `≤ 6x` gate.
- FullPipeline tracks Lowering plus verification; 20k is ~21 ms on P-cores.

## Artifact and sidecars

5k / 10k entities:

| Benchmark           |        5k |        10k | 2x input |
| ------------------- | --------: | ---------: | -------: |
| WriteAbix           | 20.229 ms |  42.486 ms |    2.10x |
| ReadAbix            | 17.021 ms |  38.404 ms |    2.26x |
| LoadArtifact (full) | 91.358 ms | 197.657 ms |    2.16x |
| LoadAbiOnly         | 16.507 ms |  37.401 ms |    2.27x |
| LoadWithSemantic    | 61.275 ms | 133.081 ms |    2.17x |
| LoadWithLowering    | 55.130 ms | 108.595 ms |    1.97x |
| BuildMetadata       |  1.566 ms |   3.198 ms |    2.04x |
| VerifyMetadata      |  1.036 ms |   2.063 ms |    1.99x |
| ModuleFromRegion    |  0.764 ms |   1.561 ms |    2.04x |
| Materialize         |  0.089 ms |   0.177 ms |    1.99x |
| MaterializeRegion   |  0.853 ms |   1.725 ms |    2.02x |

Sidecar cost is visible at 5k:

```text
LoadAbiOnly       16.5 ms   (≈ ReadAbix 17.0 ms)
+ lowering        55.1 ms
+ semantic        61.3 ms
full load         91.4 ms
```

`LoadOptions` lets backends skip `.semantic`; generation only needs ABI + lowering. `amc build` / `amc verify` keep the full load because they need both sidecars.

## Cache

200 / 1000 entries:

| Benchmark         |      200 |      1000 | 5x input |
| ----------------- | -------: | --------: | -------: |
| StoreCold         | 2.964 ms | 15.306 ms |    5.16x |
| LookupHit         | 1.141 ms |  6.029 ms |    5.28x |
| LookupMiss        | 0.282 ms |  1.415 ms |    5.02x |
| LookupConcurrent  | 0.406 ms |  1.977 ms |    4.87x |
| VerifyAll         | 1.029 ms |  5.459 ms |    5.31x |
| DependencyChanged |   5.4 µs |    5.4 µs |    1.00x |

`LookupMissDiagnose/200` is intentionally separate: **55.8 ms** at 200 entries. It scans manifests to explain the miss, which is the old behaviour and is only enabled when a caller asks for diagnosis. The default miss path is `not_found` in ~1.4 ms at 1000 entries.

Cache writes are process-safe: entries are staged in a process-unique temporary directory and committed with rename; concurrent writers converge on one entry.

## Query and context

5k / 20k types and functions:

| Benchmark         |        5k |       20k | 4x input |
| ----------------- | --------: | --------: | -------: |
| TypeExact         |    8.2 µs |   26.9 µs |    3.38x |
| TypeSubstring     |   26.7 µs |  102.3 µs |    3.78x |
| TypeHex           |    3.9 µs |   18.1 µs |    4.50x |
| FunctionExact     |   12.6 µs |   41.5 µs |    3.15x |
| TypesText         |  1.794 ms |  7.290 ms |    4.06x |
| TypesJson         |  2.508 ms | 10.284 ms |    4.10x |
| FunctionsText     |  1.555 ms |  6.329 ms |    4.07x |
| FunctionsJson     |  2.962 ms | 12.891 ms |    4.35x |
| ContextLlm        |  9.655 ms | 42.025 ms |    4.35x |
| ContextLlmStream  |  9.705 ms | 40.856 ms |    4.21x |
| ContextJson       | 10.828 ms | 46.660 ms |    4.31x |
| ContextJsonStream | 10.512 ms | 44.699 ms |    4.25x |

Notes:

- Exact name/ID lookup is microseconds; the old unconditional type index was removed.
- Text/JSON/context rendering scales linearly with output size.
- Streaming context avoids materializing the whole document; it is slightly faster and is used by `amc context -o` and stdout.

### Why not `llvm::json::OStream`?

`llvm::json::OStream` was evaluated and rejected for this workload. In a direct comparison it was ~3x slower on short, attribute-heavy objects:

```text
ContextJson / 5k    10.7 ms -> 20.1 ms
TypesJson / 5k       2.5 ms ->  7.6 ms
FunctionsJson / 5k   3.0 ms ->  9.1 ms
```

The final design keeps the manual JSON writer but streams directly into `llvm::raw_ostream`. The string API is a thin wrapper, so callers that need a value keep the old behaviour, while CLI/file output avoids the full copy.

## Multi-file parallelism

AMC does **not** embed a process pool. Like `clang` under Ninja, multi-file throughput comes from the build system launching independent `amc` processes. This keeps global state isolated and avoids a cross-file thread-safety audit.

### External processes

`amc generate` at 1–32 concurrent processes, 5 repetitions:

| instances | mean wall | files/sec | speedup |
| --------: | --------: | --------: | ------: |
|         1 |  14.62 ms |     68.41 |   1.00x |
|         2 |  14.11 ms |    141.72 |   2.07x |
|         4 |  15.14 ms |    264.21 |   3.86x |
|         8 |  14.78 ms |    541.15 |   7.91x |
|        16 |  15.53 ms |   1030.57 |  15.06x |
|        32 |  28.15 ms |   1136.68 |  16.62x |

16 processes track the P-core count; 32 oversubscribes and throughput flattens. The multi-process harness is not CPU-pinned; use it as a scaling indicator, not a P-core absolute.

### Shared cache

Several `amc build` processes, same config, distinct `-B`, one `--cache`:

| instances |     cold |    warm | artifact digest | cache entries |
| --------: | -------: | ------: | --------------- | ------------: |
|         8 | 21.35 ms | 7.41 ms | all identical   |             1 |
|        16 | 19.98 ms | 7.21 ms | all identical   |             1 |

Every process uses a process-unique frontend/verify temporary path. Concurrent writers stage cache entries independently and only one entry is committed; all outputs hash to the same artifact.

### In-process reference

`amc-multifile-perf-bench` also measures a read-only artifact load with `llvm::DefaultThreadPool`:

| files | sequential | thread pool | speedup |
| ----: | ---------: | ----------: | ------: |
|     1 |   3.743 ms |    3.800 ms |   0.98x |
|     2 |   7.399 ms |    4.000 ms |   1.85x |
|     4 |  14.509 ms |    4.180 ms |   3.47x |
|     8 |  28.577 ms |    6.441 ms |   4.44x |
|    16 |  58.329 ms |   10.077 ms |   5.79x |

This is useful evidence that read-only loading has no shared-state hazard, but it is **not** the product model. The product model is external processes.

## Optimization history

| Change                   |           Before |    After |
| ------------------------ | ---------------: | -------: |
| Lowering / 20k           |         552.7 ms |  10.1 ms |
| FullPipeline / 20k       |         525.7 ms |  21.2 ms |
| Compatibility / 20k      |         455.4 ms |  15.5 ms |
| Cache LookupMiss / 1000  |          1.454 s | 1.415 ms |
| Query FunctionExact / 5k |        370.96 µs |  12.6 µs |
| LoadAbiOnly / 5k         |   91.4 ms (full) |  16.5 ms |
| ContextJsonStream / 20k  | 46.7 ms (string) |  44.7 ms |

The large wins came from removing quadratic scans, then from loading only the sidecars a caller needs. Parallelism was deliberately the last step.

## Regression gates

A performance change is accepted only when:

- `ctest` passes (currently 27/27) and `boundary --strict` passes;
- Pass 4x input growth stays ≤ 6x;
- FullPipeline 4x growth stays ≤ 8x;
- Compatibility 4x growth stays ≤ 8x;
- Cache miss 5x growth stays ≤ 8x;
- Artifact load and query rendering stay near linear;
- no new failures in `tools/run_bench.py`;
- external multi-process throughput does not regress;
- shared-cache concurrent builds keep identical output digests.

## Caveats

- These are local P-core measurements, not a cross-machine guarantee.
- Governor, SMT, background load and memory bandwidth affect absolute numbers.
- `LookupMissDiagnose` is intentionally expensive; use the default miss path unless a precise reason is required.
- Multi-threaded benchmarks must be read with `real_time`; `cpu_time` under-reports them.
- The benchmark fixtures are synthetic; real projects may have different include trees, template instantiation costs and I/O patterns.

## Related documents

- [Module boundaries](../architecture/module-boundaries.md)
- [AMC CLI](../amc/amc.md)
- [C++ provider](../lang/cpp.md)
- Optimization history is maintained in the repository's internal working notes.
