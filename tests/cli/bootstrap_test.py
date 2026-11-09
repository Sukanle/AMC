#!/usr/bin/env python3
"""Verify AMC's own metadata bootstrap through `amc_add_abi`.

Usage:
    bootstrap_test.py <amc_bin_dir> <source_root> [--abix-include-dirs PATHLIST] [--keep-artifacts]

`amc_add_abi` (from `cmake/AMC.cmake`) attaches `self.abic.toml` to the
`tests/consumers` CMake build graph, so one `cmake --build` runs:

    amc build -> validate -> generate -> compile -> run the consumer

The consumer reads the recorded types back through
`RuntimeRegistry::type_of<T>()` in the generated C++ projection. This is the
AMC-side owner of the test formerly reached through
`ABIX/tools/run_test.py --bootstrap`.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def exe_name(base: str) -> str:
    return f"{base}.exe" if sys.platform == "win32" else base


def run_cmd(
    cmd: list[str], cwd: Path | None = None, check: bool = True
) -> subprocess.CompletedProcess[str]:
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if check and result.returncode != 0:
        raise SystemExit(
            f"Command failed (exit code {result.returncode}): {' '.join(cmd)}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def run_bootstrap_test(
    amc_bin_dir: Path, source_root: Path, include_dirs: list[str], keep: bool
) -> int:
    print("=" * 60)
    print("AMC Core Metadata Bootstrap")
    print("=" * 60)

    amc = amc_bin_dir / exe_name("amc")
    amc_cpp = amc_bin_dir / exe_name("amc-cpp")
    if not amc.is_file() or not amc_cpp.is_file():
        print(
            f"[FAIL] amc_bin_dir must contain executable amc and amc-cpp: {amc_bin_dir}",
            file=sys.stderr,
        )
        return 1

    if shutil.which("cmake") is None:
        print("[FAIL] required command not found: cmake", file=sys.stderr)
        return 1

    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    if cxx is None:
        print("[FAIL] required compiler not found: set CXX or install clang++/c++", file=sys.stderr)
        return 1

    if not include_dirs:
        print(
            "[FAIL] ABIX_INCLUDE_DIRS or --abix-include-dirs is required",
            file=sys.stderr,
        )
        return 1

    # Ninja keeps the fixture build directory free of MSVC generator artefacts;
    # fall back to the platform default when it is unavailable.
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []

    work_dir = Path(tempfile.mkdtemp(prefix="amc-bootstrap.", dir=os.environ.get("TMPDIR")))
    fixture_src = source_root / "tests" / "consumers"
    module_dir = source_root / "cmake"
    if not (module_dir / "AMC.cmake").is_file():
        module_dir = source_root / "CMake"
    artifact = work_dir / "build" / "amc_core.abix"
    header = work_dir / "generated" / "amc_core.hpp"
    consumer = work_dir / exe_name("self_consumer")

    print(f"[INFO] source root: {source_root}")
    print(f"[INFO] work dir:    {work_dir}")

    try:
        print("[STEP] configure the amc_add_abi consumer fixture")
        cmake_cmd = [
            "cmake",
            "-S",
            str(fixture_src),
            "-B",
            str(work_dir),
            *generator,
            f"-DCMAKE_CXX_COMPILER={cxx}",
            f"-DAMC_MODULE_DIR={module_dir}",
            f"-DAMC_SOURCE_ROOT={source_root}",
            f"-DAMC_EXECUTABLE={amc}",
            "-DABIX_INCLUDE_DIRS=" + ";".join(include_dirs),
        ]
        run_cmd(cmake_cmd)

        print("[STEP] extract AMC core metadata and compile the consumer")
        run_cmd(["cmake", "--build", str(work_dir), "--target", "self_consumer", "--parallel"])

        if not artifact.is_file():
            print(f"[FAIL] expected artifact was not produced: {artifact}", file=sys.stderr)
            return 1

        print("[STEP] validate and inspect artifact")
        run_cmd([str(amc), "validate", str(artifact)])
        inspect_result = run_cmd([str(amc), "inspect", str(artifact)])
        inspect_output = inspect_result.stdout.strip()
        print(f"[INFO] {inspect_output}")

        if "package=amc_core" not in inspect_output:
            print("[FAIL] artifact package is not amc_core", file=sys.stderr)
            return 1
        if "types=" not in inspect_output:
            print("[FAIL] artifact contains no types", file=sys.stderr)
            return 1
        types_match = re.search(r"types=(\d+)", inspect_output)
        if types_match is None or int(types_match.group(1)) == 0:
            print("[FAIL] artifact contains no types", file=sys.stderr)
            return 1

        print("[STEP] inspect the generated C++ metadata projection")
        if not header.is_file():
            print(f"[FAIL] expected generated header was not produced: {header}", file=sys.stderr)
            return 1

        header_text = header.read_text()
        if "amc_ABIModule_ABIX" not in header_text:
            print("[FAIL] projection lacks ABIModule descriptor", file=sys.stderr)
            return 1
        if "amc_MapOperation_ABIX" not in header_text:
            print("[FAIL] projection lacks MapOperation descriptor", file=sys.stderr)
            return 1

        print("[STEP] run the native type_of<T>() consumer")
        run_cmd([str(consumer)])

        print("[PASS] AMC core metadata bootstrap succeeded")
        print("[PASS] verified amc_add_abi -> build -> validate -> generate -> RuntimeRegistry::type_of<T>()")
        return 0
    finally:
        if keep:
            print(f"[INFO] retained artifacts: {work_dir}")
        else:
            shutil.rmtree(work_dir, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="AMC core metadata bootstrap test")
    parser.add_argument("amc_bin_dir", help="Directory containing amc and amc-cpp")
    parser.add_argument("source_root", help="Root of the AMC source tree")
    parser.add_argument(
        "--abix-include-dirs",
        default=os.environ.get("ABIX_INCLUDE_DIRS", ""),
        help="ABIX/MICS include roots, separated by the platform path separator",
    )
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        default=os.environ.get("KEEP_ARTIFACTS", "") == "1",
        help="Retain the temporary build directory on exit",
    )
    args = parser.parse_args()

    include_dirs = [p for p in args.abix_include_dirs.split(os.pathsep) if p]
    return run_bootstrap_test(
        Path(args.amc_bin_dir), Path(args.source_root), include_dirs, args.keep_artifacts
    )


if __name__ == "__main__":
    sys.exit(main())
