#!/usr/bin/env python3
"""Measure external multi-process scaling for `amc`.

AMC does not embed a process pool: like `clang`/`gcc` under Ninja, the build
system owns multi-file parallelism and launches one `amc` process per job. This
harness reproduces that model by starting N independent `amc generate`
processes at once and measuring wall-clock throughput.

Usage:
    python tools/run_multiprocess_bench.py \
        --artifact build/Release/Aue/generated/build/build/counter.abix \
        --instances 1 2 4 8 16 --repetitions 3

The benchmark intentionally does not use a product process pool; `subprocess`
here is only the external build-system stand-in.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run_case(
    amc: Path,
    artifact: Path,
    language: str,
    instances: int,
    repetitions: int,
    timeout: float,
) -> list[float]:
    samples: list[float] = []
    for _ in range(repetitions):
        with tempfile.TemporaryDirectory(prefix="amc-multiprocess-") as tmp:
            directory = Path(tmp)
            processes: list[subprocess.Popen[str]] = []
            start = time.perf_counter()
            for index in range(instances):
                output = directory / f"projection-{index}.out"
                processes.append(
                    subprocess.Popen(
                        [str(amc), "generate", str(artifact), "-l", language, "-o", str(output)],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                )
            failures: list[str] = []
            for index, process in enumerate(processes):
                try:
                    _, stderr = process.communicate(timeout=timeout)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
                    failures.append(f"instance {index}: timeout")
                    continue
                if process.returncode != 0:
                    failures.append(f"instance {index}: exit {process.returncode}: {stderr.strip()}")
            elapsed = time.perf_counter() - start
            if failures:
                raise RuntimeError("; ".join(failures))
            samples.append(elapsed)
    return samples


def main() -> int:
    parser = argparse.ArgumentParser(description="External multi-process `amc` scaling benchmark")
    parser.add_argument("--amc", default="build/Release/bin/amc", help="path to the amc executable")
    parser.add_argument("--artifact", required=True, help="input .abix artifact")
    parser.add_argument("--language", default="cpp", help="backend language passed to `amc generate`")
    parser.add_argument("--instances", type=int, nargs="+", default=[1, 2, 4, 8], help="process counts")
    parser.add_argument("--repetitions", type=int, default=3, help="repetitions per process count")
    parser.add_argument("--timeout", type=float, default=120.0, help="per-process timeout in seconds")
    parser.add_argument("--out", help="optional JSON output path")
    args = parser.parse_args()

    amc = Path(args.amc).resolve()
    artifact = Path(args.artifact).resolve()
    if not amc.is_file():
        print(f"amc executable not found: {amc}", file=sys.stderr)
        return 1
    if not artifact.is_file():
        print(f"artifact not found: {artifact}", file=sys.stderr)
        return 1

    results = []
    baseline_rate = None
    for instances in args.instances:
        samples = run_case(amc, artifact, args.language, instances, args.repetitions, args.timeout)
        mean = sum(samples) / len(samples)
        rate = instances / mean
        if baseline_rate is None:
            baseline_rate = rate
        results.append(
            {
                "instances": instances,
                "mean_seconds": mean,
                "files_per_second": rate,
                "speedup": rate / baseline_rate if baseline_rate else 0.0,
                "samples_seconds": samples,
            }
        )

    print("| instances | mean wall ms | files/sec | speedup |")
    print("|---:|---:|---:|---:|")
    for row in results:
        print(
            f"| {row['instances']} | {row['mean_seconds'] * 1000:.2f} | "
            f"{row['files_per_second']:.2f} | {row['speedup']:.2f}x |"
        )

    if args.out:
        output = Path(args.out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps({"artifact": str(artifact), "results": results}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
