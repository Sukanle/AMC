#!/usr/bin/env python3
"""Explicit Rust container contract test for `[[rust.expose]]` + `[[adapt]]`.

Usage:
    container_adapt_test.py <amc_bin_dir> [source_root]

Builds a Rust fixture whose ordinary functions take `String`, `Vec<i32>` and
`Option<i32>`. Declared `[[adapt]]` mappings turn those parameters into ABIX-STD
boundary types, AMC emits the boundary struct definitions and marshalling code,
and the test compiles the wrapper with rustc and calls it through ctypes.
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
        print("usage: container_adapt_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "tests" / "fixtures"
    if not fixtures.is_dir():
        fixtures = source_root / "AMC" / "tests" / "fixtures"
    amc = bin_dir / exe("amc")
    rustc = shutil.which("rustc")
    if not rustc:
        print("  [SKIP] rustc is required for the container adaptation test")
        return 0

    work = tempfile.mkdtemp(prefix="amc-rust-container.")
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

    def output(result):
        return (result.stdout or "") + (result.stderr or "")

    try:
        for name in ("rust_container_api.rs", "rust_container_api.abic.toml"):
            shutil.copyfile(os.path.join(fixtures, name), os.path.join(work, name))
        config = os.path.join(work, "rust_container_api.abic.toml")
        out = os.path.join(work, "out")
        artifact = os.path.join(out, "rust_container_api.abix")

        result = run([str(amc), "build", "-c", config, "-B", out])
        if not check(result.returncode == 0 and os.path.isfile(artifact),
                     "container adaptation artifact built", output(result)[-600:]):
            return 1

        thunks_path = os.path.join(work, "container_thunks.rs")
        thunks = open(thunks_path, encoding="utf-8").read() if os.path.isfile(thunks_path) else ""
        check("pub struct abix_str_utf8" in thunks and "pub reserved: u32" in thunks,
              "StringView boundary struct is generated")
        check("pub struct abix_span_i32" in thunks and "pub data: *const i32" in thunks,
              "SequenceView boundary struct is generated")
        check("pub struct abix_optional_i32" in thunks and "MaybeUninit<i32>" in thunks,
              "Optional boundary struct is generated")
        check("from_utf8_lossy" in thunks and ".into_owned()" in thunks,
              "String parameter is marshalled from the StringView")
        check(".to_vec()" in thunks, "Vec parameter is marshalled from the SequenceView")
        check("discriminant == 1" in thunks and "assume_init()" in thunks,
              "Option parameter is marshalled from the Optional boundary")
        check("from_utf8_unchecked" in thunks, "borrowed &str parameter is zero-copy")

        for function, expected in (
            ("container_string_len", "abix_str_utf8"),
            ("container_vec_sum", "abix_span_i32"),
            ("container_option_value", "abix_optional_i32"),
            ("container_str_view_len", "abix_str_utf8"),
            ("container_slice_sum", "abix_span_i32"),
        ):
            result = run([str(amc), "query", artifact, "--function", function,
                          "--format", "json"])
            if check(result.returncode == 0, f"{function} is recorded", output(result)[-300:]):
                match = json.loads(result.stdout).get("matches", [{}])[0]
                parameter = (match.get("parameters") or [{}])[0]
                check(parameter.get("type_name") == expected,
                      f"{function} takes the adapted boundary type")

        lib_rs = os.path.join(work, "lib.rs")
        with open(lib_rs, "w", encoding="utf-8") as handle:
            handle.write('include!("rust_container_api.rs");\n')
            handle.write('include!("container_thunks.rs");\n')
        library = module_path(work, "rust_container_api")
        result = run([rustc, "--edition", "2021", "--crate-type", "cdylib", lib_rs, "-o", library])
        if not check(result.returncode == 0, "generated container wrappers compile", output(result)[-600:]):
            return 1

        class AbixStr(ctypes.Structure):
            _fields_ = [("data", ctypes.POINTER(ctypes.c_uint8)),
                        ("len", ctypes.c_uint64),
                        ("encoding", ctypes.c_uint32),
                        ("reserved", ctypes.c_uint32)]

        class AbixSpanI32(ctypes.Structure):
            _fields_ = [("data", ctypes.POINTER(ctypes.c_int32)),
                        ("len", ctypes.c_uint64)]

        class AbixOptionalI32(ctypes.Structure):
            _fields_ = [("discriminant", ctypes.c_uint32),
                        ("reserved", ctypes.c_uint32),
                        ("value", ctypes.c_int32)]

        lib = ctypes.CDLL(library)

        text = b"hello"
        buffer = ctypes.create_string_buffer(text)
        string_view = AbixStr(ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint8)), len(text), 0, 0)
        lib.container_string_len.argtypes = [AbixStr]
        lib.container_string_len.restype = ctypes.c_size_t
        check(lib.container_string_len(string_view) == len(text),
              "StringView parameter reaches the Rust String function")

        values = (ctypes.c_int32 * 4)(1, 2, 3, 4)
        span = AbixSpanI32(values, 4)
        lib.container_vec_sum.argtypes = [AbixSpanI32]
        lib.container_vec_sum.restype = ctypes.c_int32
        check(lib.container_vec_sum(span) == 10,
              "SequenceView parameter reaches the Rust Vec function")

        lib.container_option_value.argtypes = [AbixOptionalI32]
        lib.container_option_value.restype = ctypes.c_int32
        check(lib.container_option_value(AbixOptionalI32(1, 0, 42)) == 42,
              "Optional parameter reaches the Rust Option function")
        check(lib.container_option_value(AbixOptionalI32(0, 0, 0)) == -1,
              "Optional None is preserved")

        lib.container_str_view_len.argtypes = [AbixStr]
        lib.container_str_view_len.restype = ctypes.c_size_t
        check(lib.container_str_view_len(string_view) == len(text),
              "borrowed &str parameter is zero-copy")
        lib.container_slice_sum.argtypes = [AbixSpanI32]
        lib.container_slice_sum.restype = ctypes.c_int32
        check(lib.container_slice_sum(span) == 10,
              "borrowed &[T] parameter is zero-copy")

        # A container parameter without a declared mapping still fails closed.
        no_adapt = os.path.join(work, "no_adapt.abic.toml")
        with open(no_adapt, "w", encoding="utf-8") as handle:
            handle.write("""[package]
name = "rust_container_no_adapt"
version = "0.1"

[[import]]
language = "rust"
files = ["rust_container_api.rs"]
symbols = ["ContainerApi"]

[[rust.expose]]
function = "container_string_len"
as = "container_string_len"

[target]
arch = 0
os = 0
abi = 1
pointer_bits = 64
endianness = "little"

[thunks]
output = "no_adapt_thunks.rs"

[[export]]
output = "rust_container_no_adapt.abix"
""")
        result = run([str(amc), "build", "-c", no_adapt, "-B", os.path.join(work, "no_adapt_out")])
        no_adapt_output = output(result)
        check(result.returncode != 0 and "Rust String is an owned buffer" in no_adapt_output,
              "container parameter without [[adapt]] fails closed", no_adapt_output[-500:])

        # Owned container returns are not silently borrowed.
        return_config = os.path.join(work, "return_adapt.abic.toml")
        with open(config, encoding="utf-8") as handle:
            text = handle.read()
        text = text.replace(
            '[[rust.expose]]\nfunction = "container_string_len"\nas = "container_string_len"',
            '[[rust.expose]]\nfunction = "container_make_string"\nas = "container_make_string"\n\n'
            '[[rust.expose]]\nfunction = "container_string_len"\nas = "container_string_len"', 1)
        with open(return_config, "w", encoding="utf-8") as handle:
            handle.write(text)
        result = run([str(amc), "build", "-c", return_config, "-B", os.path.join(work, "return_out")])
        return_output = output(result)
        check(result.returncode != 0 and "needs an explicit owned contract" in return_output,
              "container return without an owned contract fails closed", return_output[-500:])

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
