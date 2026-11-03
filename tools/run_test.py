#!/usr/bin/env python3
"""Run AMC test suites through the repository's CTest registration.

Default: run every registered CTest suite. The tier flags select one group and
map to the same CTest names, so the unified runner and a direct `ctest` call
cannot drift apart. The MCP compatibility flow runs directly because it needs a
host and a plugin artifact.

Usage:
    python tools/run_test.py                         # all CTest suites
    python tools/run_test.py --core                  # amc-core-test
    python tools/run_test.py --e2e                   # amc-e2e-test
    python tools/run_test.py --feature               # amc-feature-test
    python tools/run_test.py --interop               # interop / expose / panic
    python tools/run_test.py --elf-pe                # amc-elf-pe-metadata-test
    python tools/run_test.py --bootstrap             # amc-bootstrap-test
    python tools/run_test.py --list                  # list registered tests
    python tools/run_test.py --mcp --host h.abix --plugin p.abix
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Tier flags select CTest names. A test may appear in more than one tier when
# the same workflow is useful from several entry points.
TIERS = {
    "core": ["amc-core-test"],
    "e2e": ["amc-e2e-test"],
    "package": ["amc-package-test"],
    "feature": ["amc-feature-test"],
    "rust": ["amc-rust-test", "amc-rust-panic-test"],
    "interop": ["amc-interop-test", "amc-expose-interop-test"],
    "cancellation": ["amc-cancellation-test"],
    "cmake_integration": ["amc-cmake-integration-test"],
    "elf_pe": ["amc-elf-pe-metadata-test"],
    "bootstrap": ["amc-bootstrap-test"],
}


def exe_name(base: str) -> str:
    return f"{base}.exe" if sys.platform == "win32" else base


def build_dir_for(value: str) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else PROJECT_ROOT / path


def ctest_env(keep: bool) -> dict[str, str] | None:
    if not keep:
        return None
    return {**os.environ, "KEEP_ARTIFACTS": "1"}


def run_registered_tests(
    build_dir: Path,
    selected: list[str],
    list_only: bool,
    keep: bool,
    verbose: bool,
    extra_args: list[str],
) -> int:
    if not (build_dir / "CTestTestfile.cmake").is_file():
        print(f"\033[31mError: no CTest registration in {build_dir}\033[0m")
        print("Build first: python tools/run_build.py")
        return 1

    cmd = ["ctest", "--test-dir", str(build_dir)]
    if list_only:
        cmd.append("-N")
    else:
        cmd.append("--output-on-failure")
        if verbose:
            cmd.append("-V")

    if selected:
        pattern = "|".join(re.escape(name) for name in sorted(set(selected)))
        cmd.extend(["-R", f"^({pattern})$"])

    cmd.extend(extra_args)
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    return subprocess.run(cmd, cwd=PROJECT_ROOT, env=ctest_env(keep)).returncode


def run_mcp_test(
    build_dir: Path,
    host: str,
    plugin: str,
    amc_mcp: str,
    keep: bool,
) -> int:
    script = PROJECT_ROOT / "tests" / "cli" / "mcp_compat_test.py"
    if not script.is_file():
        print(f"\033[31mError: MCP compatibility test not found: {script}\033[0m")
        return 1

    executable = Path(amc_mcp).expanduser() if amc_mcp else build_dir / "bin" / exe_name("amc-mcp")
    if not executable.is_file():
        print(f"\033[31mError: amc-mcp not found: {executable}\033[0m")
        print("Build first: python tools/run_build.py")
        return 1

    cmd = [
        sys.executable,
        str(script),
        "--host",
        host,
        "--plugin",
        plugin,
        "--amc-mcp",
        str(executable),
    ]
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    env = ctest_env(keep)
    result = subprocess.run(cmd, cwd=PROJECT_ROOT, env=env)
    if result.returncode == 0:
        print("\033[32m[PASS] MCP compatibility test\033[0m")
    else:
        print(f"\033[31m[FAIL] MCP compatibility test (exit {result.returncode})\033[0m")
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run AMC test suites",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--build-dir",
        default="build/Release",
        help="Build directory (default: build/Release)",
    )
    parser.add_argument(
        "--list", action="store_true", dest="list_only", help="List registered CTest tests"
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Keep temporary artifacts (sets KEEP_ARTIFACTS=1)",
    )
    parser.add_argument("--verbose", action="store_true", help="Verbose CTest output")

    tier_group = parser.add_argument_group("test tiers")
    for key in TIERS:
        if key == "elf_pe":
            tier_group.add_argument(
                "--elf-pe", action="store_true", help="ELF/Mach-O/PE metadata section test"
            )
        elif key == "cmake_integration":
            tier_group.add_argument(
                "--cmake-integration", action="store_true", help="CMake integration test"
            )
        else:
            tier_group.add_argument(f"--{key}", action="store_true", help=f"{key} test tier")

    mcp_group = parser.add_argument_group("MCP compatibility")
    mcp_group.add_argument(
        "--mcp",
        action="store_true",
        help="Run the amc-mcp compatibility flow (requires --host and --plugin)",
    )
    mcp_group.add_argument("--host", default="", help="host .abix or binary")
    mcp_group.add_argument("--plugin", default="", help="plugin .abix or binary")
    mcp_group.add_argument(
        "--amc-mcp",
        default="",
        help="Path to amc-mcp (default: <build-dir>/bin/amc-mcp)",
    )

    parser.add_argument(
        "--ctest-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="Extra argument passed to ctest (repeatable)",
    )
    args = parser.parse_args()

    build_dir = build_dir_for(args.build_dir)

    if args.mcp:
        if not args.host or not args.plugin:
            parser.error("--mcp requires --host and --plugin")
        return run_mcp_test(
            build_dir, args.host, args.plugin, args.amc_mcp, args.keep
        )

    selected: list[str] = []
    for key, names in TIERS.items():
        if getattr(args, key):
            selected.extend(names)

    return run_registered_tests(
        build_dir,
        selected,
        args.list_only,
        args.keep,
        args.verbose,
        args.ctest_arg,
    )


if __name__ == "__main__":
    sys.exit(main())
