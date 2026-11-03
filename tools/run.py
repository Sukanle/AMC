#!/usr/bin/env python3
"""Unified entry point for the AMC project.

Orchestrates the pipeline: build -> test.

Usage:
    python tools/run.py                        # build + test
    python tools/run.py --build-only           # build only
    python tools/run.py --test-only            # test only (skip build)
    python tools/run.py --clean                # clean + build + test
    python tools/run.py --build-type Debug     # Debug build
    python tools/run.py --elf-pe               # build + ELF/PE metadata test
    python tools/run.py --bootstrap            # build + metadata bootstrap test
    python tools/run.py --interop              # build + interop tests
    python tools/run.py --mcp --host h.abix --plugin p.abix

All build/test options are forwarded to the stage scripts:
  - run_build.py:  --clean, --build-type, --cmake-var, --no-tests, ...
  - run_test.py:   --core, --e2e, --feature, --interop, --elf-pe, ...
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent


def run_script(script_name: str, args: list[str]) -> int:
    """Run a Python script from the tools directory."""
    script = TOOLS_DIR / script_name
    if not script.exists():
        print(f"\033[31mError: script not found: {script}\033[0m", file=sys.stderr)
        return 1
    cmd = [sys.executable, str(script), *args]
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    return subprocess.run(cmd, cwd=TOOLS_DIR.parent).returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Unified entry point for the AMC project (build + test)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Pipeline stages:
  build     Configure and build AMC
  test      Run the registered AMC CTest suites

Examples:
  python tools/run.py                        # build + test
  python tools/run.py --build-only           # build only
  python tools/run.py --test-only            # test only
  python tools/run.py --clean --build-type Debug
  python tools/run.py --elf-pe               # ELF/PE metadata test only
  python tools/run.py --bootstrap            # bootstrap test only
""",
    )

    pipeline = parser.add_mutually_exclusive_group()
    pipeline.add_argument(
        "--build-only", action="store_true", help="Build only, skip tests"
    )
    pipeline.add_argument(
        "--test-only", action="store_true", help="Test only, skip build"
    )

    build_group = parser.add_argument_group("build options (forwarded to run_build.py)")
    build_group.add_argument(
        "--clean", action="store_true", help="Clean the build directory first"
    )
    build_group.add_argument(
        "--build-type", default="Release", help="CMake build type (Release/Debug)"
    )
    build_group.add_argument(
        "--build-dir", default="", help="Build directory (default: build/<type>)"
    )
    build_group.add_argument(
        "--generator", default="", help="CMake generator (auto-detect by default)"
    )
    build_group.add_argument(
        "--cmake-env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Environment variable for CMake (repeatable)",
    )
    build_group.add_argument(
        "--cmake-var",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="CMake cache variable (repeatable)",
    )
    build_group.add_argument(
        "--target", action="append", default=[], help="Build only this target (repeatable)"
    )
    build_group.add_argument(
        "--no-tests", action="store_true", help="Configure with AMC_BUILD_TESTS=OFF"
    )
    build_group.add_argument(
        "--no-install", action="store_true", help="Configure with AMC_INSTALL=OFF"
    )
    build_group.add_argument(
        "--no-lldb", action="store_true", help="Configure with AMC_ENABLE_LLDB_PLUGIN=OFF"
    )
    build_group.add_argument(
        "--no-aue", action="store_true", help="Configure with AMC_BUILD_AUE=OFF"
    )
    build_group.add_argument(
        "--verbose", action="store_true", help="Verbose CMake build output"
    )

    test_group = parser.add_argument_group("test options (forwarded to run_test.py)")
    test_group.add_argument("--core", action="store_true", help="amc-core-test")
    test_group.add_argument("--e2e", action="store_true", help="amc-e2e-test")
    test_group.add_argument("--package", action="store_true", help="amc-package-test")
    test_group.add_argument("--feature", action="store_true", help="amc-feature-test")
    test_group.add_argument("--rust", action="store_true", help="Rust provider tests")
    test_group.add_argument(
        "--interop", action="store_true", help="interop and expose interop tests"
    )
    test_group.add_argument(
        "--cancellation", action="store_true", help="provider cancellation test"
    )
    test_group.add_argument(
        "--cmake-integration", action="store_true", help="CMake integration test"
    )
    test_group.add_argument(
        "--elf-pe", action="store_true", help="ELF/Mach-O/PE metadata section test"
    )
    test_group.add_argument(
        "--bootstrap", action="store_true", help="AMC metadata bootstrap test"
    )
    test_group.add_argument(
        "--keep", action="store_true", help="Keep temporary test artifacts"
    )
    test_group.add_argument(
        "--list",
        action="store_true",
        dest="list_only",
        help="List registered CTest tests",
    )
    test_group.add_argument(
        "--ctest-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="Extra argument passed to ctest (repeatable)",
    )
    test_group.add_argument(
        "--mcp",
        action="store_true",
        help="Run the amc-mcp compatibility flow (requires --host and --plugin)",
    )
    test_group.add_argument("--host", default="", help="host .abix or binary")
    test_group.add_argument("--plugin", default="", help="plugin .abix or binary")
    test_group.add_argument(
        "--amc-mcp",
        default="",
        help="Path to amc-mcp (default: <build-dir>/bin/amc-mcp)",
    )

    args = parser.parse_args()

    if args.mcp and (not args.host or not args.plugin):
        parser.error("--mcp requires --host and --plugin")

    should_build = not args.test_only
    should_test = not args.build_only

    build_dir = args.build_dir or f"build/{args.build_type}"

    exit_code = 0

    if should_build:
        print("\n" + "=" * 70)
        print("  BUILD STAGE")
        print("=" * 70)
        build_args = [
            "--build-type",
            args.build_type,
            "--build-dir",
            build_dir,
        ]
        if args.clean:
            build_args.append("--clean")
        if args.generator:
            build_args.extend(["--generator", args.generator])
        for item in args.cmake_env:
            build_args.extend(["--cmake-env", item])
        for item in args.cmake_var:
            build_args.extend(["--cmake-var", item])
        for target in args.target:
            build_args.extend(["--target", target])
        if args.no_tests:
            build_args.append("--no-tests")
        if args.no_install:
            build_args.append("--no-install")
        if args.no_lldb:
            build_args.append("--no-lldb")
        if args.no_aue:
            build_args.append("--no-aue")
        if args.verbose:
            build_args.append("--verbose")

        rc = run_script("run_build.py", build_args)
        if rc != 0:
            print(f"\033[31mBuild failed with exit code {rc}\033[0m", file=sys.stderr)
            return rc

    if should_test:
        print("\n" + "=" * 70)
        print("  TEST STAGE")
        print("=" * 70)
        test_args = ["--build-dir", build_dir]
        if args.keep:
            test_args.append("--keep")
        if args.list_only:
            test_args.append("--list")
        if args.verbose:
            test_args.append("--verbose")
        for key in (
            "core",
            "e2e",
            "package",
            "feature",
            "rust",
            "interop",
            "cancellation",
            "cmake_integration",
            "elf_pe",
            "bootstrap",
        ):
            if getattr(args, key):
                test_args.append("--" + key.replace("_", "-"))
        for item in args.ctest_arg:
            test_args.extend(["--ctest-arg", item])
        if args.mcp:
            test_args.extend(
                ["--mcp", "--host", args.host, "--plugin", args.plugin]
            )
            if args.amc_mcp:
                test_args.extend(["--amc-mcp", args.amc_mcp])

        rc = run_script("run_test.py", test_args)
        if rc != 0:
            print(f"\033[31mTests failed with exit code {rc}\033[0m", file=sys.stderr)
            exit_code = rc

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
