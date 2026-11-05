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
| `amc-multifile-perf-bench`| OPT-6| sequential vs thread-pool multi-file ABI load     |
| `abix-bench`              | B5   | Aue L0/L1/L2, fields, arrays and GC               |

## Run

Recommended runner (one process per executable, JSON + Markdown summary):

```bash
python tools/run_bench.py --bin build/bin --out bench/results --repetitions 5
python tools/run_bench.py --list
python tools/run_bench.py FullPipeline --exe amc-pass-perf-bench
python tools/run_bench.py --pin off
python tools/run_bench.py --pin e          # E-cores (Linux/Windows)
```

Or through the unified pipeline:

```bash
python tools/run.py --bench-only --build-dir build/Release
python tools/run.py                         # build + test + benchmark
```

`tools/run_bench.py` forwards benchmark-name regexes to Google Benchmark,
normalises `ns`/`us`/`ms`/`s` to nanoseconds, and writes one JSON file per
executable under `--out`.

Pinning is cross-platform where the OS supports it: Linux uses
`sched_setaffinity` plus cpufreq to distinguish P/E cores; Windows uses
`SetProcessAffinityMask` plus `EfficiencyClass`; macOS currently runs unpinned.

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

## External multi-process scaling

AMC does not embed a process pool. Multi-file throughput comes from the build
system launching independent `amc` processes, just like Ninja launches
`clang`/`gcc`. Use the external harness to measure that model:

```bash
python tools/run_multiprocess_bench.py \
  --artifact build/Release/Aue/generated/build/build/counter.abix \
  --instances 1 2 4 8 16 --repetitions 3
```

The harness starts N `amc generate` processes at once with distinct output
paths. It is not a production process pool and is not part of CTest.

For shared-cache contention, run several `amc build` processes with distinct
`-B` directories and one `--cache`:

```bash
python tools/stress_concurrent_build.py \
  --config Aue/Counter.abic.toml --instances 8 \
  --cold-rounds 2 --warm-rounds 4
```

The stress tool checks that every build succeeds, every output has the same
digest, and concurrent writers leave exactly one cache entry.

Aue's `abix-bench` remains a standalone Lua micro-benchmark because it is also
built by `Aue/standalone` without the AMC benchmark dependency.
