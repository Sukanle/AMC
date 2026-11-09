#!/usr/bin/env python3
"""Target-aware `c_long` / `c_ulong` default test.

Usage:
    c_long_default_test.py <amc_bin_dir> [source_root]

Builds the same Rust source with Linux (`os=0`), Windows (`os=1`) and macOS
(`os=2`) target facts and checks the recorded layout:

    Linux / macOS, 64-bit: c_long = 8, struct size 16, align 8
    Windows x64:           c_long = 4, struct size 8,  align 4
    explicit c_long_bits:  always wins over the OS default
"""

import json
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
        print("usage: c_long_default_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "tests" / "fixtures"
    if not fixtures.is_dir():
        fixtures = source_root / "AMC" / "tests" / "fixtures"
    amc = bin_dir / exe("amc")

    work = tempfile.mkdtemp(prefix="amc-rust-clong.")
    passed = failed = 0

    def check(ok, message, detail=""):
        nonlocal passed, failed
        if ok:
            passed += 1
            print("  [PASS] " + message)
        else:
            failed += 1
            print("  [FAIL] " + message + ((" :: " + detail) if detail else ""))
        return ok

    def run(args, **kwargs):
        return subprocess.run(args, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", **kwargs)

    def output(r):
        return (r.stdout or "") + (r.stderr or "")

    def build_case(name, os_value, c_long_bits=None):
        config = os.path.join(work, name + ".abic.toml")
        target = [
            "[target]",
            "arch = 0",
            "os = " + str(os_value),
            "abi = 1",
            "pointer_bits = 64",
            'endianness = "little"',
        ]
        if c_long_bits is not None:
            target.append("c_long_bits = " + str(c_long_bits))
        text = "\n".join([
            "[package]",
            'name = "rust_clongs"',
            'version = "0.1"',
            "",
            "[[import]]",
            'language = "rust"',
            'files = ["rust_clongs.rs"]',
            'symbols = ["RustCLongs"]',
            "",
            *target,
            "",
            "[[export]]",
            'output = "rust_clongs.abix"',
            "",
        ])
        with open(config, "w", encoding="utf-8") as handle:
            handle.write(text)
        out = os.path.join(work, name)
        artifact = os.path.join(out, "rust_clongs.abix")
        r = run([str(amc), "build", "-c", config, "-B", out])
        if not check(r.returncode == 0 and os.path.isfile(artifact),
                     f"{name} artifact built", output(r)[-400:]):
            return None
        r = run([str(amc), "query", artifact, "--type", "RustCLongs",
                 "--format", "json", "--layout"])
        if not check(r.returncode == 0, f"{name} layout queried", output(r)[-400:]):
            return None
        document = json.loads(r.stdout)
        matches = document.get("matches", [])
        if not check(len(matches) == 1, f"{name} has one layout match"):
            return None
        layout = matches[0]
        fields = {field["name"]: field["offset"] for field in layout.get("fields", [])}
        return layout.get("size"), layout.get("align"), fields

    try:
        shutil.copyfile(os.path.join(fixtures, "rust", "layouts", "rust_clongs.rs"),
                        os.path.join(work, "rust_clongs.rs"))

        linux = build_case("linux", 0)
        if linux:
            size, align, fields = linux
            check(size == 16 and align == 8, "Linux x64 c_long is 8 bytes", f"size={size} align={align}")
            check(fields.get("signed") == 0 and fields.get("unsigned") == 8,
                  "Linux x64 c_long offsets", str(fields))

        macos = build_case("macos", 2)
        if macos:
            size, align, fields = macos
            check(size == 16 and align == 8, "macOS x64 c_long is 8 bytes", f"size={size} align={align}")

        windows = build_case("windows", 1)
        if windows:
            size, align, fields = windows
            check(size == 8 and align == 4, "Windows x64 c_long defaults to 4 bytes", f"size={size} align={align}")
            check(fields.get("signed") == 0 and fields.get("unsigned") == 4,
                  "Windows x64 c_long offsets", str(fields))

        override = build_case("windows_override", 1, c_long_bits=64)
        if override:
            size, align, fields = override
            check(size == 16 and align == 8, "explicit c_long_bits overrides the OS default",
                  f"size={size} align={align}")

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
