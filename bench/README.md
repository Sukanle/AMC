# AMC Benchmarks

Google Benchmark baselines for the v0.3.0 benchmark plan.

## Build

Google Benchmark is optional. When `AMC_BUILD_BENCHMARKS=ON` and the package is
available, `bench/` is added by the top-level CMake:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DAMC_BUILD_BENCHMARKS=ON \
  -DCMAKE_PREFIX_PATH=<prefixes>
cmake --build build --parallel
```

If Google Benchmark is not found, correctness targets are still built and the
performance targets are skipped with a CMake status message.

## Targets

| Target                    | Plan | Purpose                                           |
| ------------------------- | ---- | ------------------------------------------------- |
| `amc-pass-perf-bench`     | B1   | semantic/ABI/lowering/compatibility pipeline      |
| `amc-artifact-perf-bench` | B2   | `.abix`, sidecars, metadata region, materialize   |
| `amc-cache-perf-bench`    | B3   | cold store, hit/miss, dependencies, concurrency   |
| `amc-query-perf-bench`    | B4   | type/function lookup, text/JSON/context rendering |
| `abix-bench`              | B5   | Aue L0/L1/L2, fields, arrays and GC               |

## Run

Short CTest smoke:

```bash
ctest -L perf --output-on-failure
```

Full local baseline:

```bash
build/bin/amc-artifact-perf-bench \
  --benchmark_min_time=1s \
  --benchmark_repetitions=5 \
  --benchmark_format=json > artifact.json
```

Google Benchmark names are stable enough to compare across commits:

```text
ArtifactFixture/WriteAbix/5000
ArtifactFixture/ReadAbix/5000
CacheReadFixture/LookupHit/1000
QueryFixture/ContextLlm/5000
PassFixture/FullPipeline/5000
```

`CacheReadFixture/LookupConcurrent` creates its worker threads inside the
measured region; thread creation is intentionally part of that micro-benchmark.

Aue's `abix-bench` remains a standalone Lua micro-benchmark because it is also
built by `Aue/standalone` without the AMC benchmark dependency.
