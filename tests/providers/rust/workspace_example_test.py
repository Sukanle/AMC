#!/usr/bin/env python3
"""End-to-end test for `examples/rust/ffi-workspace`.

Usage:
    workspace_example_test.py <amc_bin_dir> [source_root]

Copies the example into a temporary directory, then runs its `run.sh`: AMC
generates the provider thunks and Rust projection, Cargo builds the provider
cdylib and consumer binary, and the consumer runs with `deny(unsafe_code)`.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def exe(name):
    return name + ".exe" if sys.platform == "win32" else name


def main():
    if len(sys.argv) < 2:
        print("usage: workspace_example_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else bin_dir.parent.parent
    amc = bin_dir / exe("amc")
    example = source_root / "examples" / "rust" / "ffi-workspace"
    if not example.is_dir():
        print("  [FAIL] Rust workspace example is missing", file=sys.stderr)
        return 1
    if sys.platform == "win32" or not shutil.which("bash"):
        print("  [SKIP] the Rust workspace example needs bash")
        return 0
    if not shutil.which("cargo") or not shutil.which("rustc"):
        print("  [SKIP] cargo and rustc are required for the Rust workspace example")
        return 0
    if not amc.is_file():
        print("  [FAIL] amc binary is missing", file=sys.stderr)
        return 1

    work = Path(tempfile.mkdtemp(prefix="amc-rust-workspace."))
    shutil.copytree(example, work, dirs_exist_ok=True)
    env = dict(os.environ)
    env["AMC"] = str(amc)
    env["CARGO_TARGET_DIR"] = str(work / "target")
    env["CARGO_NET_OFFLINE"] = "true"
    result = subprocess.run(
        [shutil.which("bash"), str(work / "run.sh")],
        cwd=work,
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    output = (result.stdout or "") + (result.stderr or "")
    if result.returncode == 0 and "amc ffi workspace: OK" in output:
        print("  [PASS] Rust workspace example builds and runs without unsafe")
        return 0
    print("  [FAIL] Rust workspace example failed", file=sys.stderr)
    print(output[-1200:], file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
