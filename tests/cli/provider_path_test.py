#!/usr/bin/env python3
"""Provider lookup regression test.

Usage:
    provider_path_test.py <amc_bin_dir> [source_root]

Runs `amc` through PATH (a bare argv[0], not an absolute path) and checks that
the frontend provider is still resolved next to the real executable. This
covers the installed-tool case where the current directory is not the binary
directory.
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
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    keep = "--keep-artifacts" in sys.argv[1:] or os.environ.get("KEEP_ARTIFACTS", "") == "1"
    if not argv:
        print("usage: provider_path_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "tests" / "fixtures"
    if not fixtures.is_dir():
        fixtures = source_root / "AMC" / "tests" / "fixtures"
    if not (bin_dir / exe("amc")).is_file():
        print(f"amc not found in {bin_dir}", file=sys.stderr)
        return 2

    env = dict(os.environ)
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")

    # Windows' CreateProcess resolves a bare program name against the *parent*
    # process PATH, not against the PATH supplied through `env`. Resolve `amc`
    # through the child PATH explicitly and pass it as `executable`, so argv[0]
    # stays a bare name and the provider lookup still runs against the real
    # executable location.
    run_kwargs = {}
    if sys.platform == "win32":
        amc = shutil.which(exe("amc"), path=env["PATH"])
        if amc is None:
            print(f"amc not found on PATH: {bin_dir}", file=sys.stderr)
            return 2
        run_kwargs["executable"] = amc

    work = tempfile.mkdtemp(prefix="amc-provider-path.")

    try:
        for name in ("rust_demo.rs", "rust_demo.abic.toml"):
            shutil.copyfile(os.path.join(fixtures, "rust", "demo", name), os.path.join(work, name))
        config = os.path.join(work, "rust_demo.abic.toml")
        out = os.path.join(work, "out")
        result = subprocess.run(
            [exe("amc"), "build", "-c", config, "-B", out],
            capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, cwd=work, **run_kwargs)
        output = (result.stdout or "") + (result.stderr or "")
        artifact = os.path.join(out, "rust_demo.abix")
        ok = result.returncode == 0 and os.path.isfile(artifact)
        if ok:
            print("  [PASS] amc invoked through PATH resolves its provider")
        else:
            print("  [FAIL] amc invoked through PATH resolves its provider")
            print("         " + output.strip()[:400])
        if keep:
            print("  kept artifacts in " + work)
        return 0 if ok else 1
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
