#!/usr/bin/env python3
"""Configure and build the AMC project.

This script only builds. Use:
  - python tools/run_test.py    for tests
  - python tools/run.py         for the full pipeline (build + test)

Usage:
    python tools/run_build.py                          # default Release build
    python tools/run_build.py --clean                  # clean + build
    python tools/run_build.py --build-type Debug       # Debug build
    python tools/run_build.py --no-tests               # skip the test targets
    python tools/run_build.py --no-install             # skip install rules
    python tools/run_build.py --cmake-var KEY=VALUE    # pass CMake variables
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def run(
    cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None
) -> None:
    """Run a command and raise SystemExit on failure."""
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    child_env = None if env is None else {**os.environ, **env}
    result = subprocess.run(cmd, cwd=cwd, env=child_env)
    if result.returncode != 0:
        raise SystemExit(
            f"Command failed (exit code {result.returncode}): {' '.join(cmd)}"
        )


def parse_key_values(items: list[str], flag: str) -> dict[str, str]:
    """Parse repeatable `KEY=VALUE` flags into a dict (the last value wins)."""
    parsed: dict[str, str] = {}
    for item in items:
        key, separator, value = item.partition("=")
        if not separator or not key:
            raise SystemExit(f"{flag} expects KEY=VALUE, got: {item!r}")
        parsed[key] = value
    return parsed


def build_dir_for(args: argparse.Namespace) -> Path:
    if args.build_dir:
        path = Path(args.build_dir).expanduser()
        return path if path.is_absolute() else PROJECT_ROOT / path
    return PROJECT_ROOT / "build" / args.build_type


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure and build AMC",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--clean", action="store_true", help="Clean the build directory first"
    )
    parser.add_argument(
        "--build-type", default="Release", help="CMake build type (Release/Debug)"
    )
    parser.add_argument(
        "--build-dir", default="", help="Build directory (default: build/<type>)"
    )
    parser.add_argument(
        "--generator", default="", help="CMake generator (auto-detect by default)"
    )
    parser.add_argument(
        "--cmake-env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Environment variable for CMake (repeatable)",
    )
    parser.add_argument(
        "--cmake-var",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="CMake cache variable, forwarded as -DKEY=VALUE (repeatable)",
    )
    parser.add_argument(
        "--target", action="append", default=[], help="Build only this target (repeatable)"
    )
    parser.add_argument(
        "--no-tests", action="store_true", help="Configure with AMC_BUILD_TESTS=OFF"
    )
    parser.add_argument(
        "--no-install", action="store_true", help="Configure with AMC_INSTALL=OFF"
    )
    parser.add_argument(
        "--no-lldb", action="store_true", help="Configure with AMC_ENABLE_LLDB_PLUGIN=OFF"
    )
    parser.add_argument(
        "--no-aue", action="store_true", help="Configure with AMC_BUILD_AUE=OFF"
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Verbose CMake build output"
    )
    args = parser.parse_args()

    cmake_env = parse_key_values(args.cmake_env, "--cmake-env")
    cmake_vars = parse_key_values(args.cmake_var, "--cmake-var")
    build_dir = build_dir_for(args)

    if args.clean and build_dir.exists():
        print(f"== Cleaning build directory: {build_dir} ==")
        shutil.rmtree(build_dir)

    generator = args.generator
    if not generator:
        if shutil.which("ninja"):
            generator = "Ninja"
        elif sys.platform == "win32":
            generator = "MinGW Makefiles"
        else:
            generator = "Unix Makefiles"

    configure = [
        "cmake",
        "-S",
        str(PROJECT_ROOT),
        "-B",
        str(build_dir),
        "-G",
        generator,
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        f"-DAMC_BUILD_TESTS={'OFF' if args.no_tests else 'ON'}",
        f"-DAMC_INSTALL={'OFF' if args.no_install else 'ON'}",
        f"-DAMC_ENABLE_LLDB_PLUGIN={'OFF' if args.no_lldb else 'ON'}",
        f"-DAMC_BUILD_AUE={'OFF' if args.no_aue else 'ON'}",
    ]
    configure.extend(f"-D{key}={value}" for key, value in cmake_vars.items())

    print(f"== Using generator: {generator} ==")
    run(configure, env=cmake_env)

    build = ["cmake", "--build", str(build_dir), "--parallel"]
    for target in args.target:
        build.extend(["--target", target])
    if args.verbose:
        build.append("--verbose")
    run(build, env=cmake_env)

    print("== Build complete ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
