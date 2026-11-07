# AMC Benchmarks

<p align="center">
  English · <a href="README_ZH.md">中文</a>
</p>

This section records AMC's performance baselines, scaling behaviour and regression gates. It is evidence for the optimization plan, not a marketing page: every number below comes from a Release build, a pinned P-core set and Google Benchmark JSON.

> Baseline snapshot: **v0.3.2 / 2026-11-07**. The tree includes the C++ frontend/backend split, shared CodeIn/Codegen helpers, OPT-4/OPT-6/OPT-7, the single-file cache rework and the target-aware Rust FFI surface.

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

- Release build, Google Benchmark, `--benchmark_min_time=0.2s`; the original pass/artifact/query tables use 3 repetitions, the cache retest and Windows comparison use 5.
- CPU affinity restricted to the P-core set (`tools/run_bench.py --pin p`).
- Values are **wall time** (`real_time`). `cpu_time` is not used for multi-threaded or multi-process cases because it only counts the calling thread.
- Inputs are generated deterministically by each fixture. Page cache is warm; this measures AMC, not cold disk I/O.
- `ctest -L perf` runs a short 0.05s smoke version of every suite.
- The 2026-11-07 Linux retest kept Pass, Artifact, Query and Multifile within ±3%, so those tables keep the 2026-11-06 values; only Cache is updated. Absolute values still drift with compiler, CPU and governor.

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

200 / 1000 entries, P-core, mean wall time. This is the Linux retest after the single-file entry rework and the POSIX `stat` fast path (2026-11-07); pre-rework numbers are in [Optimization history](#optimization-history).

| Benchmark         |      200 |     1000 | 5x input |
| ----------------- | -------: | -------: | -------: |
| StoreCold         | 1.668 ms | 8.444 ms |    5.06x |
| LookupHit         | 1.044 ms | 5.407 ms |    5.18x |
| LookupHitShallow  | 0.723 ms | 3.796 ms |    5.25x |
| LookupMiss        | 0.290 ms | 1.437 ms |    4.96x |
| LookupConcurrent  | 0.371 ms | 1.795 ms |    4.84x |
| VerifyAll         | 0.845 ms | 4.381 ms |    5.18x |
| DependencyChanged |   5.1 µs |   5.1 µs |    1.00x |

New cache cases:

| Benchmark                  |      200 |      1000 |
| -------------------------- | -------: | --------: |
| LookupHitWithDeps (8 deps) | 2.595 ms | 23.735 ms |
| LookupHitLarge / 8 (1 MiB) |        — |  6.515 ms |

Per-entry cost at 1000 entries: `StoreCold` 8.4 µs, `LookupHit` 5.4 µs, shallow hit 3.8 µs, miss 1.4 µs.

The rework replaced the per-key directory with a single versioned entry file, stopped re-reading payloads during publish, opens the entry directly instead of doing a separate existence check, and maps payloads above 64 KiB. A POSIX-only `stat` fast path detects a missing entry before constructing an `ifstream`; Windows keeps direct open because `CreateFile`-based metadata is much more expensive there.

Compared with the pre-rework baseline, `StoreCold` is 45% faster, `VerifyAll` 20% faster and `LookupHit` 10% faster. `LookupMiss/1000` is 1.437 ms versus 1.415 ms (+1.5%, about 0.02 µs per lookup); the POSIX fast path recovered the +15% regression that direct open introduced, at the cost of about 4% on the hit path (5.407 ms versus 5.178 ms with direct open, still below the 6.029 ms pre-rework hit). `LookupMissDiagnose/200` is 52.9 ms and remains a separate diagnostic path that scans manifests.

Cache writes are process-safe: entries are staged in a process-unique temporary file and committed with rename; concurrent writers converge on one entry.

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

## Cross-platform: Windows and CachyOS

The same machine and the same Release build, measured on Windows (MinGW/UCRT64 clang) and CachyOS. The Windows side used `--pin p`, real-time protection off, the high-performance power plan, `--benchmark_repetitions=5`, `--benchmark_min_time=0.2s`, and was launched from pwsh so shell environment variables stay out of the picture.

### Pass (ms, 5k / 20k)

| Benchmark      |        CachyOS |        Windows |         Ratio |
| -------------- | -------------: | -------------: | ------------: |
| SemanticVerify |  0.724 / 3.158 |  1.048 / 4.863 | 1.45x / 1.54x |
| AbiVerify      |  1.260 / 5.581 |  2.157 / 9.612 | 1.71x / 1.72x |
| Lowering       | 1.995 / 10.080 | 2.617 / 13.903 | 1.31x / 1.38x |
| LoweringVerify | 2.869 / 13.296 | 3.869 / 20.338 | 1.35x / 1.53x |
| FullPipeline   | 4.698 / 21.150 | 6.978 / 31.394 | 1.49x / 1.48x |
| Compatibility  | 3.576 / 15.518 | 3.961 / 17.379 | 1.11x / 1.12x |

### Artifact (ms, 5k / 10k)

| Benchmark         |          CachyOS |           Windows |         Ratio |
| ----------------- | ---------------: | ----------------: | ------------: |
| WriteAbix         |  20.229 / 42.486 |   25.778 / 57.089 | 1.27x / 1.34x |
| ReadAbix          |  17.021 / 38.404 |   21.592 / 49.257 | 1.27x / 1.28x |
| LoadAbiOnly       |  16.507 / 37.401 |   21.780 / 49.282 | 1.32x / 1.32x |
| LoadWithSemantic  | 61.275 / 133.081 |  76.694 / 171.786 | 1.25x / 1.29x |
| LoadWithLowering  | 55.130 / 108.595 |  76.768 / 170.008 | 1.39x / 1.57x |
| LoadArtifact      | 91.358 / 197.657 | 143.004 / 295.432 | 1.57x / 1.49x |
| BuildMetadata     |    1.566 / 3.198 |     2.424 / 4.890 | 1.55x / 1.53x |
| VerifyMetadata    |    1.036 / 2.063 |     1.045 / 2.087 | 1.01x / 1.01x |
| ModuleFromRegion  |    0.764 / 1.561 |     0.801 / 1.780 | 1.05x / 1.14x |
| Materialize       |    0.089 / 0.177 |     0.092 / 0.188 | 1.03x / 1.06x |
| MaterializeRegion |    0.853 / 1.725 |     0.907 / 1.993 | 1.06x / 1.16x |

### Cache (ms, 200 / 1000 entries)

The CachyOS column is the 2026-11-07 retest after the single-file entry rework and the POSIX `stat` fast path. The Windows column includes the same single-file entry, direct open, on-demand verification and mapping above 64 KiB; the POSIX `stat` fast path does not apply there.

| Benchmark                |       CachyOS | Windows (optimized) |         Ratio |
| ------------------------ | ------------: | ------------------: | ------------: |
| StoreCold                | 1.668 / 8.444 |    49.340 / 250.868 | 29.6x / 29.7x |
| LookupHit                | 1.044 / 5.407 |      9.405 / 48.017 |   9.0x / 8.9x |
| LookupHitShallow         | 0.723 / 3.796 |      4.435 / 22.798 |   6.1x / 6.0x |
| LookupMiss               | 0.290 / 1.437 |       1.676 / 8.833 |   5.8x / 6.1x |
| LookupConcurrent         | 0.371 / 1.795 |      3.597 / 17.267 |   9.7x / 9.6x |
| VerifyAll                | 0.845 / 4.381 |      9.979 / 51.538 | 11.8x / 11.8x |
| DependencyChanged        |        5.1 µs |               48 µs |          9.3x |
| LookupMissDiagnose / 200 |      52.86 ms |              506 ms |          9.6x |
| LookupHitLarge / 8       |      6.515 ms |            8.565 ms |         1.31x |

`LookupHitWithDeps` is intentionally not in the table: Windows measured ~52 ms for 1000 entries under normal conditions, but the same binary saw 645 ms in one outlier run. Dependency reads follow the host filesystem state.

The CachyOS/Windows ratio grows after the rework because Linux got much faster; the residual Windows cost is NTFS metadata, not an algorithmic difference.

### Query and multi-file

- Exact queries are actually faster on Windows: `TypeExact` 0.95x / 0.85x, `TypeSubstring` 0.77x / 0.77x, `FunctionExact` 0.90x / 0.93x (5k / 20k).
- Rendering and context are 1.1–1.3x slower: `TypesJson` 1.32x / 1.33x, `ContextJson` 1.26x / 1.26x.
- Multi-file runs are 1.17–1.22x slower sequentially; the thread pool is faster on Windows at 8 / 16 files (parallel speedup 6.40x vs 4.44x, 6.88x vs 5.79x).

### Reading the numbers

- **No platform-specific complexity regression**: every 4x / 2x / 5x growth curve matches CachyOS, and the gates above hold on Windows as well.
- The constant factors form three bands: `VerifyMetadata`, `Materialize` and exact queries at about 1.0x; most passes, sequential artifact I/O and JSON rendering at 1.2–1.6x; the cache at 5–30x (widest for `StoreCold`, where Linux dropped to 8.4 µs/entry).
- Exact queries being faster on Windows shows the gap is not code generation but **allocator plus filesystem**; the cache residue is NTFS metadata.
- Per-entry cache cost on Windows before/after the optimization: `StoreCold` 533 → 251 µs, `LookupHit` 96 → 48 µs, shallow hit 23 µs.

### Measurement caveats

- `sd` in the tables is within-run noise; cross-run absolutes drift by 0.5–1.4x, so prefer same-run comparisons.
- `AbiVerify` once reported 17.8 ms on Windows (stable within that run); three reruns gave 9.7–10.6 ms, so that sample is excluded.
- Dependency reads follow the host filesystem: the same binary measured `LookupHitWithDeps/1000` at 52 ms → 645 ms → 52 ms within one day.
- The Windows numbers come from a single run and are a reference, not a replacement for the CachyOS baseline.

## Optimization history

| Change                                |           Before |    After |
| ------------------------------------- | ---------------: | -------: |
| Lowering / 20k                        |         552.7 ms |  10.1 ms |
| FullPipeline / 20k                    |         525.7 ms |  21.2 ms |
| Compatibility / 20k                   |         455.4 ms |  15.5 ms |
| Cache LookupMiss / 1000               |          1.454 s | 1.415 ms |
| Query FunctionExact / 5k              |        370.96 µs |  12.6 µs |
| LoadAbiOnly / 5k                      |   91.4 ms (full) |  16.5 ms |
| ContextJsonStream / 20k               | 46.7 ms (string) |  44.7 ms |
| Cache StoreCold / 1000 (Linux)        |        15.306 ms | 8.444 ms |
| Cache LookupHit / 1000 (Linux)        |         6.029 ms | 5.407 ms |
| Cache VerifyAll / 1000 (Linux)        |         5.459 ms | 4.381 ms |
| Cache LookupHitShallow / 1000 (Linux) |                — | 3.796 ms |
| Cache LookupMiss / 1000 (Linux)       |         1.415 ms | 1.437 ms |
| Cache StoreCold / 1000 (Windows)      |         533.1 ms | 250.9 ms |
| Cache LookupHit / 1000 (Windows)      |          96.1 ms |  48.0 ms |

The large wins came from removing quadratic scans, then from loading only the sidecars a caller needs, and finally from replacing the per-key cache directory with a single entry file. Parallelism was deliberately the last step.

`LookupMiss/1000` on Linux briefly moved from 1.415 ms to a stable 1.627 ms mean (+15%) after direct open. The POSIX-only `stat` fast path brought it back to 1.437 ms (+1.5%). `LookupHit/1000` is 5.407 ms, about 4% slower than direct open but still 10% faster than pre-rework. `LookupMissDiagnose` remains a separate scan path.

## Regression gates

A performance change is accepted only when:

- `ctest` passes (currently 27/27) and `boundary --strict` passes;
- Pass 4x input growth stays ≤ 6x;
- FullPipeline 4x growth stays ≤ 8x;
- Compatibility 4x growth stays ≤ 8x;
- Cache miss 5x growth stays ≤ 8x; the POSIX `stat` fast path keeps 1000-entry miss at +1.5% (1.437 ms vs 1.415 ms), while hit remains 10% faster than pre-rework;
- Artifact load and query rendering stay near linear;
- no new failures in `tools/run_bench.py`;
- external multi-process throughput does not regress;
- shared-cache concurrent builds keep identical output digests;
- Windows meets the same growth gates (see "Cross-platform: Windows and CachyOS").

## Caveats

- These are local P-core measurements, not a cross-machine guarantee.
- Governor, SMT, background load and memory bandwidth affect absolute numbers.
- Windows and Linux absolutes depend on the allocator and filesystem; the cache's 5–30x gap is NTFS metadata cost, not an algorithmic difference. See "Cross-platform: Windows and CachyOS".
- `LookupMissDiagnose` is intentionally expensive; use the default miss path unless a precise reason is required.
- Multi-threaded benchmarks must be read with `real_time`; `cpu_time` under-reports them.
- The benchmark fixtures are synthetic; real projects may have different include trees, template instantiation costs and I/O patterns.

## Related documents

- [Module boundaries](../architecture/module-boundaries.md)
- [AMC CLI](../amc/amc.md)
- [C++ provider](../lang/cpp.md)
- Optimization history is maintained in the repository's internal working notes.
