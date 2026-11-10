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
            shutil.copyfile(os.path.join(fixtures, "rust", "containers", name), os.path.join(work, name))
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
        check("pub struct abix_span_ContainerPoint" in thunks
              and "pub data: *const ContainerPoint" in thunks,
              "non-primitive SequenceView element is generated")
        check("pub struct abix_optional_ContainerPoint" in thunks
              and "MaybeUninit<ContainerPoint>" in thunks,
              "non-primitive Optional element is generated")
        check("MaybeUninit::new(value)" in thunks and "MaybeUninit::uninit()" in thunks,
              "Option return is marshalled into the Optional boundary")
        check("pub struct abix_owned_str_utf8" in thunks
              and "pub struct abix_owner_handle" in thunks
              and "__amc_owned_str_destroy" in thunks,
              "owned String return uses the ABIX-owned view")
        check("pub struct abix_owned_span_ContainerPoint" in thunks
              and "__amc_owned_span_ContainerPoint_destroy" in thunks,
              "owned Vec return uses the ABIX-owned span view")

        for function, expected in (
            ("container_string_len", "abix_str_utf8"),
            ("container_vec_sum", "abix_span_i32"),
            ("container_option_value", "abix_optional_i32"),
            ("container_str_view_len", "abix_str_utf8"),
            ("container_slice_sum", "abix_span_i32"),
            ("container_points_sum", "abix_span_ContainerPoint"),
            ("container_option_point", "abix_optional_ContainerPoint"),
        ):
            result = run([str(amc), "query", artifact, "--function", function,
                          "--format", "json"])
            if check(result.returncode == 0, f"{function} is recorded", output(result)[-300:]):
                match = json.loads(result.stdout).get("matches", [{}])[0]
                parameter = (match.get("parameters") or [{}])[0]
                check(parameter.get("type_name") == expected,
                      f"{function} takes the adapted boundary type")

        for function, expected in (
            ("container_option_make", "abix_optional_i32"),
            ("container_owned_string", "abix_owned_str_utf8"),
            ("container_owned_points", "abix_owned_span_ContainerPoint"),
        ):
            result = run([str(amc), "query", artifact, "--function", function, "--format", "json"])
            if check(result.returncode == 0, f"{function} is recorded", output(result)[-300:]):
                match = json.loads(result.stdout).get("matches", [{}])[0]
                check(match.get("return_type_name") == expected,
                      f"{function} uses the expected boundary return type")

        gen = os.path.join(work, "gen_owner")
        result = run([str(amc), "generate", artifact, "-l", "rust", "-o", gen])
        if check(result.returncode == 0, "owned view Rust projection generated", output(result)[-400:]):
            projection = open(os.path.join(gen, "amc_generated.rs"), encoding="utf-8").read()
            check("pub struct OwnedStringUtf8" in projection and "pub struct OwnedSpanContainerPoint" in projection,
                  "owned view RAII wrappers are generated")
            check("impl Drop for OwnedStringUtf8" in projection and "owner.destroy" in projection,
                  "owned view Drop calls the owner destroy")

        lib_rs = os.path.join(work, "lib.rs")
        with open(lib_rs, "w", encoding="utf-8") as handle:
            handle.write('include!("rust_container_api.rs");\n')
            handle.write('include!("container_thunks.rs");\n')
        library = module_path(work, "rust_container_api")
        result = run([rustc, "--edition", "2021", "--crate-type", "cdylib", lib_rs, "-o", library])
        if not check(result.returncode == 0, "generated container wrappers compile", output(result)[-600:]):
            return 1

        cpp_header = os.path.join(work, "owner_gen.hpp")
        result = run([str(amc), "generate", artifact, "-l", "cpp", "-o", cpp_header])
        if check(result.returncode == 0, "owned view C++ projection generated", output(result)[-400:]):
            header = open(cpp_header, encoding="utf-8").read()
            check("class OwnedStringUtf8" in header and "reinterpret_cast<void (*)(void *)>" in header,
                  "C++ owned string RAII wrapper is generated")
            check("class OwnedSpanContainerPoint" in header
                  and "using abix_owned_span_ContainerPoint = ::abix_std::abix_owned_span<void>;" in header,
                  "C++ arbitrary-element owned span RAII wrapper is generated")
            check("template <class T> const T *data() const" in header,
                  "C++ owned span exposes typed data()")
            cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
            if cxx:
                include_flags = [
                    "-I" + work,
                    "-I" + str(source_root / "include"),
                    "-I" + str(source_root / "dependencies" / "ABIX" / "include"),
                    "-I" + str(source_root / "dependencies" / "ABIX" / "MICS" / "include"),
                    "-I" + str(source_root / "dependencies" / "ABIX" / "MICS" / "HLMD"),
                ]
                cpp_consumer = os.path.join(work, "owner_consumer.cpp")
                with open(cpp_consumer, "w", encoding="utf-8") as handle:
                    handle.write("""#include "owner_gen.hpp"

extern "C" {
::abix_owned_str_utf8 container_owned_string(int32_t value);
::skl::amc::generated::abix_owned_span_ContainerPoint container_owned_points(int32_t value);
}

int main() {
    {
        auto string = ::skl::amc::generated::OwnedStringUtf8(container_owned_string(7));
        if (string.view().len != 1 || string.view().data[0] != '7') return 1;
    }
    {
        auto span = ::skl::amc::generated::OwnedSpanContainerPoint(container_owned_points(5));
        if (span.view().len != 1) return 2;
        if (span.data<int32_t>()[0] != 5 || span.data<int32_t>()[1] != 6) return 3;
    }
    return 0;
}
""")
                result = run([cxx, "-std=c++17", "-fsyntax-only"] + include_flags + [cpp_consumer])
                check(result.returncode == 0, "C++ owned view projection compiles", output(result)[-500:])
                if sys.platform != "win32":
                    cpp_consumer_bin = os.path.join(work, exe("owner_consumer_cpp"))
                    link = ["-L" + work, "-lrust_container_api", "-Wl,-rpath," + work]
                    result = run([cxx, "-std=c++17"] + include_flags + [cpp_consumer, "-o", cpp_consumer_bin] + link)
                    if check(result.returncode == 0, "C++ owned view consumer linked", output(result)[-500:]):
                        env = dict(os.environ)
                        env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                        env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
                        result = run([cpp_consumer_bin], env=env)
                        check(result.returncode == 0,
                              "C++ owned view RAII wrappers release through the owner", output(result)[-500:])

        owner_consumer = os.path.join(work, "owner_consumer.rs")
        with open(owner_consumer, "w", encoding="utf-8") as handle:
            handle.write("""#[path = "gen_owner/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}
use skl::abix::*;

fn main() {
    unsafe {
        let string = OwnedStringUtf8::from_raw(container_owned_string(7)).expect("owned string");
        assert_eq!(string.view().len, 1);
        assert_eq!(*string.view().data, b'7');
        drop(string);

        let span = OwnedSpanContainerPoint::from_raw(container_owned_points(5)).expect("owned span");
        assert_eq!(span.view().len, 1);
        assert_eq!((*span.view().data).x, 5);
        drop(span);
    }
    println!("owned views: OK");
}
""")
        owner_consumer_bin = os.path.join(work, exe("owner_consumer"))
        link = ["-L", "native=" + work, "-l", "dylib=rust_container_api"]
        if sys.platform != "win32":
            link += ["-C", "link-arg=-Wl,-rpath," + work]
        result = run([rustc, "--edition", "2021", owner_consumer, "-o", owner_consumer_bin] + link)
        if check(result.returncode == 0, "owned view consumer compiled", output(result)[-500:]):
            env = dict(os.environ)
            if sys.platform == "win32":
                env["PATH"] = work + os.pathsep + env.get("PATH", "")
            else:
                env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
            result = run([owner_consumer_bin], env=env)
            check(result.returncode == 0 and "owned views: OK" in output(result),
                  "owned view RAII wrappers release through the owner", output(result)[-500:])

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

        class ContainerPoint(ctypes.Structure):
            _fields_ = [("x", ctypes.c_int32), ("y", ctypes.c_int32)]

        class AbixSpanPoint(ctypes.Structure):
            _fields_ = [("data", ctypes.POINTER(ContainerPoint)),
                        ("len", ctypes.c_uint64)]

        class AbixOptionalPoint(ctypes.Structure):
            _fields_ = [("discriminant", ctypes.c_uint32),
                        ("reserved", ctypes.c_uint32),
                        ("value", ContainerPoint)]

        class AbixOwnerHandle(ctypes.Structure):
            _fields_ = [("object", ctypes.c_void_p),
                        ("mode", ctypes.c_uint32),
                        ("flags", ctypes.c_uint32),
                        ("destroy", ctypes.c_void_p),
                        ("module_instance", ctypes.c_uint64)]

        class AbixOwnedStr(ctypes.Structure):
            _fields_ = [("view", AbixStr), ("owner", AbixOwnerHandle)]

        class AbixOwnedSpanPoint(ctypes.Structure):
            _fields_ = [("view", AbixSpanPoint), ("owner", AbixOwnerHandle)]

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

        points = (ContainerPoint * 2)(ContainerPoint(1, 2), ContainerPoint(3, 4))
        point_span = AbixSpanPoint(points, 2)
        lib.container_points_sum.argtypes = [AbixSpanPoint]
        lib.container_points_sum.restype = ctypes.c_int32
        check(lib.container_points_sum(point_span) == 10,
              "non-primitive SequenceView reaches the Rust Vec function")
        lib.container_option_point.argtypes = [AbixOptionalPoint]
        lib.container_option_point.restype = ctypes.c_int32
        check(lib.container_option_point(AbixOptionalPoint(1, 0, ContainerPoint(5, 6))) == 11,
              "non-primitive Optional reaches the Rust Option function")
        check(lib.container_option_point(AbixOptionalPoint(0, 0, ContainerPoint(0, 0))) == -1,
              "non-primitive Optional None is preserved")
        lib.container_option_make.argtypes = [ctypes.c_int32]
        lib.container_option_make.restype = AbixOptionalI32
        some = lib.container_option_make(7)
        check(some.discriminant == 1 and some.value == 7,
              "Option return produces Some")
        none = lib.container_option_make(-1)
        check(none.discriminant == 0, "Option return produces None")

        lib.container_owned_string.argtypes = [ctypes.c_int32]
        lib.container_owned_string.restype = AbixOwnedStr
        owned_str = lib.container_owned_string(7)
        check(owned_str.view.len == 1 and owned_str.view.data[0] == ord("7")
              and owned_str.owner.mode == 1 and owned_str.owner.object,
              "owned String return carries a view and an owner")
        check(owned_str.owner.object == ctypes.cast(owned_str.view.data, ctypes.c_void_p).value,
              "owned String owner.object equals view.data")
        ctypes.CFUNCTYPE(None, ctypes.c_void_p)(owned_str.owner.destroy)(owned_str.owner.object)

        lib.container_owned_points.argtypes = [ctypes.c_int32]
        lib.container_owned_points.restype = AbixOwnedSpanPoint
        owned_span = lib.container_owned_points(5)
        check(owned_span.view.len == 1 and owned_span.view.data[0].x == 5
              and owned_span.view.data[0].y == 6 and owned_span.owner.mode == 1,
              "owned Vec return carries a span view and an owner")
        check(owned_span.owner.object == ctypes.cast(owned_span.view.data, ctypes.c_void_p).value,
              "owned Vec owner.object equals view.data")
        ctypes.CFUNCTYPE(None, ctypes.c_void_p)(owned_span.owner.destroy)(owned_span.owner.object)

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
            '[[adapt]]\nnative = "String"\ntarget = "abix_owned_str_utf8"\n\n', '', 1)
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
