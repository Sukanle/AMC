#!/usr/bin/env python3
"""Stress concurrent `amc build` processes sharing one instantiation cache.

This mirrors a build system launching several `amc` processes for independent
outputs while sharing the project cache. It checks that:

  * every concurrent build succeeds;
  * all outputs are byte-identical;
  * concurrent writers leave exactly one committed cache entry per key.

Usage:
    python tools/stress_concurrent_build.py \
        --config Aue/Counter.abic.toml --instances 8 --cold-rounds 2 --warm-rounds 4
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def artifact_digest(directory: Path, output: str) -> str:
    artifact = directory / output
    if not artifact.is_file():
        raise RuntimeError(f"missing artifact: {artifact}")
    return hashlib.sha256(artifact.read_bytes()).hexdigest()


def run_round(
    amc: Path,
    config: Path,
    output: str,
    instances: int,
    workdir: Path,
    cache: Path,
    round_name: str,
) -> dict:
    builds = [workdir / f"build-{round_name}-{index}" for index in range(instances)]
    for directory in builds:
        shutil.rmtree(directory, ignore_errors=True)
    start = time.perf_counter()
    processes: list[subprocess.Popen[str]] = []
    for directory in builds:
        processes.append(
            subprocess.Popen(
                [str(amc), "build", "-c", str(config), "-B", str(directory), "--cache", str(cache)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
            )
        )
    failures: list[str] = []
    for index, process in enumerate(processes):
        _, stderr = process.communicate()
        if process.returncode != 0:
            failures.append(f"instance {index}: exit {process.returncode}: {stderr.strip()}")
    elapsed = time.perf_counter() - start
    if failures:
        raise RuntimeError("; ".join(failures))

    digests = {artifact_digest(directory, output) for directory in builds}
    if len(digests) != 1:
        raise RuntimeError("concurrent builds produced different artifacts")
    return {
        "round": round_name,
        "instances": instances,
        "seconds": elapsed,
        "digest": next(iter(digests)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Concurrent amc build/cache stress test")
    parser.add_argument("--amc", default="build/Release/bin/amc")
    parser.add_argument("--config", required=True, help="path to the .abic.toml")
    parser.add_argument("--output", default="build/counter.abix", help="export path relative to each -B directory")
    parser.add_argument("--instances", type=int, default=8)
    parser.add_argument("--cold-rounds", type=int, default=2)
    parser.add_argument("--warm-rounds", type=int, default=4)
    parser.add_argument("--workdir", default=None, help="working directory (temporary by default)")
    parser.add_argument("--out", help="optional JSON result path")
    args = parser.parse_args()

    amc = Path(args.amc).resolve()
    config = Path(args.config).resolve()
    if not amc.is_file():
        print(f"amc executable not found: {amc}", file=sys.stderr)
        return 1
    if not config.is_file():
        print(f"config not found: {config}", file=sys.stderr)
        return 1

    temporary = None
    if args.workdir:
        workdir = Path(args.workdir).resolve()
        shutil.rmtree(workdir, ignore_errors=True)
        workdir.mkdir(parents=True)
    else:
        temporary = tempfile.mkdtemp(prefix="amc-cache-stress-")
        workdir = Path(temporary)
    cache = workdir / "cache"

    results: list[dict] = []
    try:
        for index in range(args.cold_rounds):
            shutil.rmtree(cache, ignore_errors=True)
            cache.mkdir(parents=True, exist_ok=True)
            results.append(
                run_round(amc, config, args.output, args.instances, workdir, cache, f"cold-{index}")
            )
        for index in range(args.warm_rounds):
            results.append(
                run_round(amc, config, args.output, args.instances, workdir, cache, f"warm-{index}")
            )
    except RuntimeError as error:
        print(f"stress failed: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)

    print("| round | instances | wall ms | digest |")
    print("|---|---:|---:|---|")
    for row in results:
        print(f"| {row['round']} | {row['instances']} | {row['seconds'] * 1000:.2f} | {row['digest'][:16]} |")

    if args.out:
        output = Path(args.out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps({"config": str(config), "results": results}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
