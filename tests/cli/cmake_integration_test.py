#!/usr/bin/env python3
"""CMake integration test for AMC.

Usage:
    cmake_integration_test.py <amc_bin_dir> <source_root>

Behaviour:
    - Configures and builds a CMake fixture project using Ninja.
    - Verifies that the expected .abix artifact and generated header exist.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description="AMC CMake integration test")
    parser.add_argument("amc_bin_dir", help="Directory containing amc executable")
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
        help="Retain the temporary build directory on exit (or set KEEP_ARTIFACTS=1)",
    )
    args = parser.parse_args()

    amc_bin = os.path.abspath(args.amc_bin_dir)
    amc_root = os.path.abspath(args.source_root)

    build_dir = tempfile.mkdtemp(
        prefix="amc-cmake.", dir=os.environ.get("TMPDIR")
    )
    print(f"  [INFO] BUILD_DIR: {build_dir}")

    try:
        # Step 1: cmake configure
        print("  [INFO] Configuring CMake fixture...")
        fixture = os.path.join(amc_root, "tests", "cli", "cmake_fixture")
        module_dir = os.path.join(amc_root, "cmake")
        if not os.path.isfile(os.path.join(module_dir, "AMC.cmake")):
            module_dir = os.path.join(amc_root, "CMake")
        fixture_dir = os.path.join(amc_root, "tests", "fixtures")
        if not os.path.isdir(fixture):
            fixture = os.path.join(amc_root, "AMC", "tests", "cli", "cmake_fixture")
            module_dir = os.path.join(amc_root, "AMC", "cmake")
            if not os.path.isfile(os.path.join(module_dir, "AMC.cmake")):
                module_dir = os.path.join(amc_root, "AMC", "CMake")
            fixture_dir = os.path.join(amc_root, "AMC", "tests", "fixtures")
        amc = os.path.join(amc_bin, "amc.exe" if sys.platform == "win32" else "amc")
        cmake_cmd = [
            "cmake",
            "-S", fixture,
            "-B", build_dir,
            "-G", "Ninja",
            f"-DAMC_MODULE_DIR={module_dir}",
            f"-DAMC_FIXTURE_DIR={fixture_dir}",
            f"-DAMC_EXECUTABLE={amc}",
        ]
        if args.abix_include_dirs:
            include_dirs = [p for p in args.abix_include_dirs.split(os.pathsep) if p]
            cmake_cmd.append("-DABIX_INCLUDE_DIRS=" + ";".join(include_dirs))
        subprocess.run(cmake_cmd, check=True, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")

        # Step 2: cmake build
        print("  [INFO] Building CMake target...")
        subprocess.run(
            ["cmake", "--build", build_dir, "--target", "amc_cmake_consumer"],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

        # Step 3: verify artifacts
        abix_output = os.path.join(build_dir, "amc-output/build/amc_test.abix")
        hpp_output = os.path.join(build_dir, "generated/amc_metadata.hpp")

        passed = 0
        failed = 0

        if os.path.isfile(abix_output):
            print(f"  [PASS] {abix_output} exists")
            passed += 1
        else:
            print(f"  [FAIL] {abix_output} does not exist")
            failed += 1

        if os.path.isfile(hpp_output):
            print(f"  [PASS] {hpp_output} exists")
            passed += 1
        else:
            print(f"  [FAIL] {hpp_output} does not exist")
            failed += 1

        print(f"\n=== Results: {passed} passed, {failed} failed ===")
        sys.exit(failed)

    finally:
        if args.keep_artifacts:
            print(f"  [INFO] retained artifacts: {build_dir}")
        else:
            shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
