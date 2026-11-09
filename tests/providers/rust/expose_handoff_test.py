#!/usr/bin/env python3
"""Explicit `[[rust.expose]]` handoff test.

Usage:
    expose_handoff_test.py <amc_bin_dir> [source_root]

Builds a Rust fixture where ordinary safe functions are exported through
AMC-generated `extern "C"` wrappers, checks the `.abix` symbol names, compiles
the generated wrappers with rustc and calls the exported symbols through
ctypes.
"""

import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def exe(name):
    return name + ".exe" if sys.platform == "win32" else name


def module_path(work, name):
    if sys.platform == "darwin":
        return os.path.join(work, "lib" + name + ".dylib")
    if sys.platform == "win32":
        return os.path.join(work, name + ".dll")
    return os.path.join(work, "lib" + name + ".so")


def main():
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    keep = "--keep-artifacts" in sys.argv[1:] or os.environ.get("KEEP_ARTIFACTS", "") == "1"
    if not argv:
        print("usage: expose_handoff_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "tests" / "fixtures"
    if not fixtures.is_dir():
        fixtures = source_root / "AMC" / "tests" / "fixtures"
    amc = bin_dir / exe("amc")
    dump = bin_dir / exe("amc-dump")
    rustc = shutil.which("rustc")
    if not rustc:
        print("  [SKIP] rustc is required for the expose handoff test")
        return 0

    work = tempfile.mkdtemp(prefix="amc-rust-expose.")
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

    try:
        for name in ("rust_expose.rs", "rust_expose.abic.toml"):
            shutil.copyfile(os.path.join(fixtures, name), os.path.join(work, name))
        config = os.path.join(work, "rust_expose.abic.toml")
        out = os.path.join(work, "out")
        artifact = os.path.join(out, "rust_expose.abix")

        r = run([str(amc), "build", "-c", config, "-B", out])
        if not check(r.returncode == 0 and os.path.isfile(artifact),
                     "explicit rust.expose artifact built", output(r)[-400:]):
            return 1

        dump_json = os.path.join(work, "dump.json")
        r = run([str(dump), artifact, "--json", dump_json])
        if check(r.returncode == 0 and os.path.isfile(dump_json),
                 "artifact dumped as JSON", output(r)[-400:]):
            data = json.load(open(dump_json, encoding="utf-8"))
            names = {function["name"] for function in data["functions"]}
            check("rust_expose_add_exported" in names, "renamed function is recorded")
            check("rust_expose_plain" in names, "plain function without a rename is wrapped")
            check("rust_expose_unsafe" in names, "unsafe function is wrapped")
            check("rust_expose_direct" in names, "already extern C function stays selected")
            check("rust_expose_point_sum_exported" in names, "struct-taking wrapper is recorded")
            check("rust_expose_add" not in names, "source-only name is not exported")
            check("rust_expose_point_sum" not in names, "source-only struct function is not exported")

        thunks = os.path.join(work, "rust_expose_thunks.rs")
        text = open(thunks, encoding="utf-8").read() if os.path.isfile(thunks) else ""
        check(os.path.isfile(thunks), "wrapper source generated")
        check('#[export_name = "rust_expose_add_exported"]' in text
              and 'pub extern "C" fn __amc_export_rust_expose_add_exported(p0: i32, p1: i32) -> i32' in text,
              "renamed scalar wrapper has the expected signature")
        check("crate::rust_expose_add(p0, p1)" in text, "scalar wrapper calls the source function")
        check('#[export_name = "rust_expose_point_sum_exported"]' in text
              and 'pub extern "C" fn __amc_export_rust_expose_point_sum_exported(p0: RustExposePoint) -> i32' in text,
              "struct wrapper has the expected signature")
        check('#[export_name = "rust_expose_plain"]' in text,
              "plain function gets a wrapper even without a rename")
        check("unsafe { crate::rust_expose_unsafe(p0) }" in text, "unsafe call is wrapped in unsafe")
        check("fn rust_expose_direct(" not in text, "already extern C function gets no wrapper")

        lib_rs = os.path.join(work, "lib.rs")
        with open(lib_rs, "w", encoding="utf-8") as handle:
            handle.write('include!("rust_expose.rs");\n')
            handle.write('include!("rust_expose_thunks.rs");\n')
        library = module_path(work, "rust_expose")
        r = run([rustc, "--edition", "2021", "--crate-type", "cdylib", lib_rs, "-o", library])
        if check(r.returncode == 0, "generated wrappers compile with rustc", output(r)[-400:]):
            lib = ctypes.CDLL(library)
            lib.rust_expose_add_exported.argtypes = [ctypes.c_int32, ctypes.c_int32]
            lib.rust_expose_add_exported.restype = ctypes.c_int32
            check(lib.rust_expose_add_exported(2, 3) == 5, "wrapped scalar function runs")

            lib.rust_expose_plain.argtypes = [ctypes.c_int32]
            lib.rust_expose_plain.restype = ctypes.c_int32
            check(lib.rust_expose_plain(9) == 9, "plain function wrapper runs")

            lib.rust_expose_unsafe.argtypes = [ctypes.c_int32]
            lib.rust_expose_unsafe.restype = ctypes.c_int32
            check(lib.rust_expose_unsafe(11) == 11, "unsafe function wrapper runs")

            lib.rust_expose_direct.argtypes = [ctypes.c_int32]
            lib.rust_expose_direct.restype = ctypes.c_int32
            check(lib.rust_expose_direct(7) == 7, "direct extern C function runs")

            class Point(ctypes.Structure):
                _fields_ = [("x", ctypes.c_int32), ("y", ctypes.c_int32)]

            lib.rust_expose_point_sum_exported.argtypes = [Point]
            lib.rust_expose_point_sum_exported.restype = ctypes.c_int32
            check(lib.rust_expose_point_sum_exported(Point(4, 5)) == 9,
                  "wrapped struct function runs with the C layout")

        # Negative: an explicit function that does not exist must fail early.
        missing = os.path.join(work, "missing.abic.toml")
        with open(config, encoding="utf-8") as handle:
            missing_text = handle.read().replace(
                'function = "rust_expose_add"', 'function = "rust_expose_missing"', 1)
        with open(missing, "w", encoding="utf-8") as handle:
            handle.write(missing_text)
        r = run([str(amc), "build", "-c", missing, "-B", os.path.join(work, "missing_out")])
        check(r.returncode != 0 and "function not found" in output(r),
              "missing expose target is rejected", output(r)[-400:])

        # Negative: selecting a plain Rust function through symbols used to be
        # recorded as a C ABI even though no export exists.
        symbol_only = os.path.join(work, "symbol_only.abic.toml")
        with open(config, encoding="utf-8") as handle:
            text_symbol_only = handle.read()
        text_symbol_only = text_symbol_only.replace(
            'symbols = ["RustExposePoint"]', 'symbols = ["rust_expose_plain"]', 1)
        text_symbol_only = (text_symbol_only.split("[[rust.expose]]", 1)[0].rstrip()
                            + "\n\n" + text_symbol_only[text_symbol_only.index("[target]"):])
        with open(symbol_only, "w", encoding="utf-8") as handle:
            handle.write(text_symbol_only)
        r = run([str(amc), "build", "-c", symbol_only, "-B", os.path.join(work, "symbol_only_out")])
        check(r.returncode != 0 and "use [[rust.expose]]" in output(r),
              "plain function in symbols is rejected with a wrapper hint", output(r)[-400:])

        # Negative: a wrapper needs an explicit output path.
        no_thunks = os.path.join(work, "no_thunks.abic.toml")
        with open(config, encoding="utf-8") as handle:
            text_no_thunks = handle.read().replace(
                '[thunks]\noutput = "rust_expose_thunks.rs"\n\n', "", 1)
        with open(no_thunks, "w", encoding="utf-8") as handle:
            handle.write(text_no_thunks)
        r = run([str(amc), "build", "-c", no_thunks, "-B", os.path.join(work, "no_thunks_out")])
        check(r.returncode != 0 and "requires a [thunks] output path" in output(r),
              "wrapper without [thunks] output is rejected", output(r)[-400:])

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
