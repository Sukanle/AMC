#!/usr/bin/env python3
"""Unified entry point for the AMC project.

Orchestrates the pipeline: build -> test -> bench.

Usage:
    python tools/run.py                        # full pipeline
    python tools/run.py --build-only           # build only
    python tools/run.py --test-only            # test only (skip build)
    python tools/run.py --bench-only           # benchmark only (skip build/test)
    python tools/run.py --clean                # clean + build + test
    python tools/run.py --build-type Debug     # Debug build
    python tools/run.py --elf-pe               # build + ELF/PE metadata test
    python tools/run.py --bootstrap            # build + metadata bootstrap test
    python tools/run.py --interop              # build + interop tests
    python tools/run.py --mcp --host h.abix --plugin p.abix
    python tools/run.py --bench-repetitions 5 --bench-pin off

All build/test/bench options are forwarded to the stage scripts:
  - run_build.py:  --clean, --build-type, --cmake-var, --no-tests, ...
  - run_test.py:   --core, --e2e, --feature, --interop, --elf-pe, ...
  - run_bench.py:  --bench-repetitions, --bench-out, --bench-pin, ...
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
    print(f"  \033[90m{' '.join(cmd)}\033[0m", flush=True)
    return subprocess.run(cmd, cwd=TOOLS_DIR.parent).returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Unified entry point for the AMC project (build + test + bench)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Pipeline stages:
  build     Configure and build AMC
  test      Run the registered AMC CTest suites
  bench     Run Google Benchmark baselines through tools/run_bench.py

Examples:
  python tools/run.py                        # full pipeline
  python tools/run.py --build-only           # build only
  python tools/run.py --test-only            # test only
  python tools/run.py --bench-only           # benchmark only
  python tools/run.py --clean --build-type Debug
  python tools/run.py --elf-pe               # ELF/PE metadata test only
  python tools/run.py --bootstrap            # bootstrap test only
""",
    )

    pipeline = parser.add_mutually_exclusive_group()
    pipeline.add_argument(
        "--build-only", action="store_true", help="Build only, skip tests and benchmark"
    )
    pipeline.add_argument(
        "--test-only", action="store_true", help="Test only, skip build and bench"
    )
    pipeline.add_argument(
        "--bench-only", action="store_true", help="Benchmark only, skip build and test"
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

    bench_group = parser.add_argument_group("bench options (forwarded to run_bench.py)")
    bench_group.add_argument(
        "--bench-repetitions",
        type=int,
        default=5,
        help="Google Benchmark repetitions (default: 5)",
    )
    bench_group.add_argument(
        "--bench-out",
        default="",
        help="Directory for benchmark JSON output (default: <build-dir>/bench)",
    )
    bench_group.add_argument(
        "--bench-min-time",
        default="",
        help="Minimum time per benchmark (e.g. 0.2s)",
    )
    bench_group.add_argument(
        "--bench-pin",
        default=None,
        help="CPU pinning: auto, off, all, or a list like 0-7",
    )
    bench_group.add_argument(
        "--bench-filter",
        action="append",
        default=[],
        metavar="REGEX",
        help="Benchmark name filter forwarded to run_bench.py (repeatable)",
    )
    bench_group.add_argument(
        "--bench-exe",
        action="append",
        default=[],
        metavar="NAME",
        help="Benchmark executable to run (repeatable)",
    )
    bench_group.add_argument(
        "--bench-list",
        action="store_true",
        help="List benchmark names instead of running them",
    )

    args = parser.parse_args()

    if args.mcp and (not args.host or not args.plugin):
        parser.error("--mcp requires --host and --plugin")

    if args.build_only:
        should_build, should_test, should_bench = True, False, False
    elif args.test_only:
        should_build, should_test, should_bench = False, True, False
    elif args.bench_only:
        should_build, should_test, should_bench = False, False, True
    else:
        # Full pipeline: build -> test -> bench.
        should_build, should_test, should_bench = True, True, True

    build_dir = args.build_dir or f"build/{args.build_type}"

    exit_code = 0

    if should_build:
        print("\n" + "=" * 70)
        print("  BUILD STAGE")
        print("=" * 70)
        sys.stdout.flush()
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
        if should_bench:
            # Make an explicit bench stage override a previous
            # -DAMC_BUILD_BENCHMARKS=OFF cache entry.
            build_args.extend(["--cmake-var", "AMC_BUILD_BENCHMARKS=ON"])
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
        sys.stdout.flush()
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

    if should_bench:
        print("\n" + "=" * 70)
        print("  BENCH STAGE")
        print("=" * 70)
        sys.stdout.flush()
        build_path = Path(build_dir)
        if not build_path.is_absolute():
            build_path = TOOLS_DIR.parent / build_path
        bin_dir = build_path / "bin"

        bench_args = ["--bin", str(bin_dir)]
        bench_args.extend(["--out", args.bench_out or str(build_path / "bench")])
        if args.bench_repetitions != 5:
            bench_args.extend(["--repetitions", str(args.bench_repetitions)])
        if args.bench_min_time:
            bench_args.extend(["--min-time", args.bench_min_time])
        if args.bench_pin is not None:
            bench_args.extend(["--pin", args.bench_pin])
        for exe in args.bench_exe:
            bench_args.extend(["--exe", exe])
        if args.bench_list:
            bench_args.append("--list")
        bench_args.extend(args.bench_filter)

        rc = run_script("run_bench.py", bench_args)
        if rc != 0:
            print(f"\033[31mBench failed with exit code {rc}\033[0m", file=sys.stderr)
            exit_code = rc

    print("\n" + "=" * 70)
    if exit_code == 0:
        print("  \033[32mAll stages completed successfully.\033[0m")
    else:
        print(f"  \033[31mPipeline completed with errors (exit code {exit_code}).\033[0m")
    print("=" * 70)
    sys.stdout.flush()

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
