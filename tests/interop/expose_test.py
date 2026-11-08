#!/usr/bin/env python3
"""Automatic capability generation test.

Usage:
    expose_test.py <amc_bin_dir> [source_root]

Proves the P0 automation loop for a plain C++ class, with **no hand-written
adapter**: AMC reads the class AST/layout, generates the C thunks
(`abix_thunks.cpp`), records the capabilities in `.abix`, projects them to Rust,
and a Rust consumer creates the object, calls its methods and lets the creating
module destroy it.

Nothing here hand-writes a C header, a method adapter, an interface table or a
consumer binding: they are all generated. `Counter::value_` is private, so the
consumer must not reach it — the class projects as an opaque handle.
"""

import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def exe_name(base: str) -> str:
    """Append `.exe` on Windows (including MSYS2/Cygwin Python), else as-is."""
    return base + ".exe" if sys.platform == "win32" else base


IS_WINDOWS = sys.platform == "win32"
IS_MACOS = sys.platform == "darwin"


def module_path(work: str, base: str) -> str:
    """Output path of the shared module `base` (libX.so / libX.dylib / X.dll)."""
    if IS_MACOS:
        return os.path.join(work, "lib" + base + ".dylib")
    if IS_WINDOWS:
        return os.path.join(work, base + ".dll")
    return os.path.join(work, "lib" + base + ".so")


def import_lib(work: str, base: str) -> str:
    """Import library emitted next to the Windows shared module `base`."""
    return os.path.join(work, "lib" + base + ".dll.a")


def module_flags(work: str, base: str) -> list:
    """Extra linker flags used to build the shared module `base`.

    A MinGW DLL exports nothing unless asked, but the generated thunks rely on
    the import library, so Windows builds export every symbol and emit the
    `lib<base>.dll.a` that rustc links against.
    """
    if IS_WINDOWS:
        return ["-Wl,--export-all-symbols", "-Wl,--out-implib," + import_lib(work, base)]
    return []


def rustc_link(work: str, base: str, search_dirs=()) -> list:
    """rustc arguments that link a consumer against the shared module `base`."""
    args = ["-L", "native=" + work, "-l", "dylib=" + base]
    if IS_WINDOWS:
        for directory in search_dirs:
            args += ["-L", "native=" + directory]
        return args
    for directory in (work, *search_dirs):
        args += ["-C", "link-arg=-Wl,-rpath," + directory]
    return args


def runtime_env(work: str, search_dirs=()) -> dict:
    """Environment that puts the module directories on the loader search path."""
    env = dict(os.environ)
    dirs = [work, *search_dirs]
    if IS_WINDOWS:
        env["PATH"] = os.pathsep.join(dirs + [env.get("PATH", "")])
    else:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(dirs + [env.get("LD_LIBRARY_PATH", "")])
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(dirs + [env.get("DYLD_LIBRARY_PATH", "")])
    return env


def toml_path(path: str) -> str:
    """A path safe to embed in a TOML string literal.

    TOML treats a backslash as an escape introducer, so a Windows path like
    `F:\\Code\\cmdly` would fail to parse (`unknown escape sequence '\\C'`).
    Forward slashes are accepted by both TOML and the C++ compiler.
    """
    return path.replace("\\", "/")


def include_roots(source_root: str) -> tuple[str, ...]:
    """Public include roots, preferring the installed ABIX package."""
    configured = os.environ.get("ABIX_INCLUDE_DIRS")
    if configured:
        return tuple(path for path in configured.split(os.pathsep) if path)
    return (
        os.path.join(source_root, "include"),
        os.path.join(source_root, "dependencies", "ABIX", "include"),
        os.path.join(source_root, "dependencies", "ABIX", "MICS", "include"),
        os.path.join(source_root, "dependencies", "ABIX", "MICS", "HLMD"),
        os.path.join(source_root, "MICS", "include"),
        os.path.join(source_root, "MICS", "HLMD"),
    )


def include_flags(source_root: str) -> list[str]:
    return ["-I" + root for root in include_roots(source_root)]


def toml_include_flags(source_root: str) -> str:
    return ", ".join('"-I%s"' % toml_path(root) for root in include_roots(source_root))


# `--keep-artifacts`, or KEEP_ARTIFACTS=1 in the environment (set by the CMake
# option SKL_ABIX_KEEP_TEST_ARTIFACTS), retains the temporary working directory.
if "--keep-artifacts" in sys.argv:
    os.environ["KEEP_ARTIFACTS"] = "1"
KEEP_ARTIFACTS = os.environ.get("KEEP_ARTIFACTS", "") == "1"


def main() -> int:
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    if not argv:
        print("usage: expose_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = os.path.abspath(argv[0])
    source_root = os.path.abspath(argv[1]) if len(argv) > 1 else os.path.abspath(
        os.path.join(SCRIPT_DIR, "..", "..", ".."))
    amc = os.path.join(bin_dir, exe_name("amc"))
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")

    missing = []
    if not os.path.isfile(amc):
        missing.append("amc")
    if not cxx:
        missing.append("a C++ compiler")
    if not rustc:
        missing.append("rustc")
    if missing:
        print("  [SKIP] automatic capability generation needs: " + ", ".join(missing))
        return 0

    passed = 0
    failed = 0

    def check(ok, message, detail=""):
        nonlocal passed, failed
        if ok:
            passed += 1
            print("  [PASS] " + message)
        else:
            failed += 1
            print("  [FAIL] " + message + ((" :: " + detail) if detail else ""))
        return ok

    def run(args, env=None, cwd=None):
        return subprocess.run(args, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", env=env, cwd=cwd)

    work = os.environ.get("AMC_INTEROP_WORK_DIR") or tempfile.mkdtemp(prefix="amc-expose.")
    fixture = os.path.join(SCRIPT_DIR, "expose")
    for name in ("counter.hpp", "counter.abic.toml", "rust_consumer.rs", "cpp_consumer.cpp"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))

    # 1. Describe the class and generate its thunks.
    r = run([amc, "build", "-c", os.path.join(work, "counter.abic.toml"), "-B", os.path.join(work, "out")])
    if not check(r.returncode == 0, "amc build generated the metadata and thunks", (r.stdout + r.stderr)[-400:]):
        return 1
    thunks = os.path.join(work, "abix_thunks.cpp")
    check(os.path.isfile(thunks), "abix_thunks.cpp was generated")
    abix = os.path.join(work, "out", "counter_expose.abix")

    # 2. Compile the generated thunks into the producing module.
    module = module_path(work, "counter")
    r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", thunks,
             *include_flags(source_root), "-o", module]
            + module_flags(work, "counter"))
    if not check(r.returncode == 0, "the generated thunks compile", r.stderr[-400:]):
        return 1
    if IS_WINDOWS:
        # `nm` cannot read a PE DLL's export table; the import library carries
        # the exported names instead.
        nm_args = ["nm", import_lib(work, "counter")]
    elif IS_MACOS:
        nm_args = ["nm", "-gU", module]
    else:
        nm_args = ["nm", "-D", module]
    check("abix_module_query_v1" in subprocess.run(
        nm_args, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout,
          "the module exports the fixed discovery entry")

    # 3. Project the capabilities to Rust.
    gen = os.path.join(work, "gen")
    os.makedirs(gen, exist_ok=True)
    r = run([amc, "generate", abix, "-l", "rust", "-o", gen])
    if not check(r.returncode == 0, "amc generate -l rust", (r.stdout + r.stderr)[-400:]):
        return 1
    generated = open(os.path.join(gen, "amc_generated.rs"), encoding="utf-8").read()
    check("pub struct Counter([u8; 4])" in generated, "the class projects as an opaque handle")
    check("pub struct OwnedCounter" in generated, "an owner wrapper was generated")
    check("pub fn get(&mut self)" in generated and "pub fn add(&mut self" in generated,
          "the owner wrapper exposes typed methods from the lowering contract")

    # The explicit exposed-handle contract is written next to the artifact.
    lowering_path = abix + ".lowering"
    if os.path.isfile(lowering_path):
        import json
        lowering = json.load(open(lowering_path, encoding="utf-8"))
        exposed = lowering.get("exposed", [])
        method_names = {method.get("name") for record in exposed for method in record.get("methods", [])}
        if exposed and exposed[0].get("create") and {"get", "add"} <= method_names:
            check(True, "the lowering sidecar records the exposed handle and its methods")
        else:
            check(False, "the lowering sidecar records the exposed handle and its methods", str(exposed))
    else:
        check(False, "the lowering sidecar records the exposed handle and its methods", "missing " + lowering_path)

    # 4. Compile and run the Rust consumer (create / get / add / destroy).
    consumer = os.path.join(work, exe_name("rust_consumer"))
    r = run([rustc, "--edition", "2021", os.path.join(work, "rust_consumer.rs"), "-o", consumer]
            + rustc_link(work, "counter"))
    if not check(r.returncode == 0, "rustc compiled the consumer", r.stderr[-400:]):
        return 1
    env = runtime_env(work)
    r = run([consumer], env=env)
    check(r.returncode == 0 and "expose counter" in r.stdout,
          "Rust created, called and destroyed the C++ object", (r.stdout + r.stderr)[-400:])

    # 4b. The C++ projection consumes the same exposed-handle contract.
    cpp_gen = os.path.join(work, "gen_cpp")
    os.makedirs(cpp_gen, exist_ok=True)
    r = run([amc, "generate", abix, "-l", "cpp", "-o", cpp_gen])
    if check(r.returncode == 0, "amc generate -l cpp", (r.stdout + r.stderr)[-400:]):
        cpp_header = open(os.path.join(cpp_gen, "amc_generated.hpp"), encoding="utf-8").read()
        check("class CounterHandle" in cpp_header and "static CounterHandle create" in cpp_header,
              "the C++ projection emits an RAII handle and factory")
        cpp_src = os.path.join(work, "cpp_handle_consumer.cpp")
        open(cpp_src, "w", encoding="utf-8").write(
            '#include "counter.hpp"\n'
            '#include "gen_cpp/amc_generated.hpp"\n'
            'int main() {\n'
            '    auto counter = amc_generated::CounterHandle::create(40);\n'
            '    if (!counter || counter.get() != 40) return 1;\n'
            '    counter.add(2);\n'
            '    return counter.get() == 42 ? 0 : 2;\n'
            '}\n')
        cpp_bin = os.path.join(work, exe_name("cpp_handle_consumer"))
        cpp_link = ["-L" + work, "-lcounter"]
        if not IS_WINDOWS:
            cpp_link.append("-Wl,-rpath," + work)
        r = run([cxx, "-std=c++17", cpp_src, "-I" + work, *include_flags(source_root), *cpp_link,
                 "-o", cpp_bin])
        if check(r.returncode == 0, "the C++ handle consumer compiles", r.stderr[-400:]):
            r = run([cpp_bin], env=runtime_env(work))
            check(r.returncode == 0, "the C++ handle consumer ran through the generated wrapper",
                  (r.stdout + r.stderr)[-400:])

    # 4c. The Zig projection consumes the same contract. Runtime execution is
    # checked by the language toolchain when available; the generated wrapper
    # must at least compile against the module.
    zig = shutil.which("zig")
    if zig and not IS_WINDOWS and not IS_MACOS:
        zig_gen = os.path.join(work, "gen_zig")
        os.makedirs(zig_gen, exist_ok=True)
        r = run([amc, "generate", abix, "-l", "zig", "-o", zig_gen])
        if check(r.returncode == 0, "amc generate -l zig", (r.stdout + r.stderr)[-400:]):
            zig_header = open(os.path.join(zig_gen, "amc_generated.zig"), encoding="utf-8").read()
            check("pub const CounterHandle" in zig_header and "bytes: [4]u8" in zig_header,
                  "the Zig projection emits an opaque handle and RAII wrapper")
            zig_src = os.path.join(work, "zig_handle_consumer.zig")
            open(zig_src, "w", encoding="utf-8").write(
                'const abi = @import("gen_zig/amc_generated.zig");\n'
                'pub fn main() !void {\n'
                '    var counter = abi.CounterHandle.create(40) orelse return error.CreateFailed;\n'
                '    if (counter.get() != 40) return error.BadValue;\n'
                '    counter.add(2);\n'
                '    if (counter.get() != 42) return error.BadValue;\n'
                '    counter.deinit();\n'
                '}\n')
            zig_bin = os.path.join(work, exe_name("zig_handle_consumer"))
            r = run([zig, "build-exe", "zig_handle_consumer.zig", "-L.", "-lcounter", "-lc",
                     "-rpath", work, "-femit-bin=" + zig_bin], cwd=work)
            if check(r.returncode == 0, "the Zig handle consumer compiles", (r.stdout + r.stderr)[-400:]):
                r = run([zig_bin], env=runtime_env(work))
                check(r.returncode == 0, "the Zig handle consumer ran through the generated wrapper",
                      (r.stdout + r.stderr)[-400:])
    else:
        print("  [SKIP] Zig handle consumer (zig not available or unsupported platform)")

    # 5/8/9: discovery through the fixed entry, binding once, and a module lease
    # that keeps the module loaded while the object is alive.
    cpp_consumer = os.path.join(work, exe_name("cpp_consumer"))
    r = run([cxx, "-std=c++17", os.path.join(work, "cpp_consumer.cpp"),
             *include_flags(source_root), "-o", cpp_consumer])
    if not check(r.returncode == 0, "the registry consumer compiles", r.stderr[-400:]):
        return 1
    r = run([cpp_consumer, module], env=env)
    check(r.returncode == 0 and "expose registry: OK" in r.stdout,
          "registry bus discovered, bound and leased the exposed capabilities", (r.stdout + r.stderr)[-400:])

    # ABIX-STD contract mapping: exposed methods speak SequenceView / StringView
    # / Result, and the contracts are recorded and projected.
    for name in ("std_api.hpp", "std_api.abic.toml", "rust_consumer_std.rs"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))
    # The fixture includes `SKL/ABIX/STD/...` by its include root; add the source
    # root to the import flags so neither the header nor the generated thunks
    # need an absolute include path.
    std_config = os.path.join(work, "std_api.abic.toml")
    text = open(std_config, encoding="utf-8").read().replace(
        '"-std=c++17"', '"-std=c++17", ' + toml_include_flags(source_root))
    open(std_config, "w", encoding="utf-8").write(text)
    r = run([amc, "build", "-c", std_config, "-B", os.path.join(work, "std_out")])
    if not check(r.returncode == 0, "amc build for the ABIX-STD api", (r.stdout + r.stderr)[-400:]):
        return 1
    std_module = module_path(work, "metrics")
    r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", os.path.join(work, "std_thunks.cpp"),
             *include_flags(source_root), "-o", std_module] + module_flags(work, "metrics"))
    if not check(r.returncode == 0, "the ABIX-STD thunks compile", r.stderr[-400:]):
        return 1
    std_gen = os.path.join(work, "std_gen")
    os.makedirs(std_gen, exist_ok=True)
    r = run([amc, "generate", os.path.join(work, "std_out", "expose_std.abix"), "-l", "rust", "-o", std_gen])
    if not check(r.returncode == 0, "amc generate for the ABIX-STD api", (r.stdout + r.stderr)[-400:]):
        return 1
    std_consumer = os.path.join(work, exe_name("rust_consumer_std"))
    # rust_consumer_std.rs includes `gen/amc_generated.rs`; point it at the std gen.
    std_consumer_src = os.path.join(work, "rust_consumer_std.rs")
    text = open(std_consumer_src, encoding="utf-8").read().replace("gen/amc_generated.rs", "std_gen/amc_generated.rs")
    open(std_consumer_src, "w", encoding="utf-8").write(text)
    r = run([rustc, "--edition", "2021", std_consumer_src, "-o", std_consumer]
            + rustc_link(work, "metrics"))
    if not check(r.returncode == 0, "rustc compiled the ABIX-STD consumer", r.stderr[-400:]):
        return 1
    r = run([std_consumer], env=env)
    check(r.returncode == 0 and "expose std: OK" in r.stdout,
          "SequenceView / StringView / Result mapped through the exposed API", (r.stdout + r.stderr)[-400:])

    # The same Result contract is projected to C++ and Zig as a typed wrapper.
    std_abix = os.path.join(work, "std_out", "expose_std.abix")
    std_cpp_gen = os.path.join(work, "std_gen_cpp")
    os.makedirs(std_cpp_gen, exist_ok=True)
    r = run([amc, "generate", std_abix, "-l", "cpp", "-o", std_cpp_gen])
    if check(r.returncode == 0, "amc generate -l cpp for the ABIX-STD api", (r.stdout + r.stderr)[-400:]):
        std_cpp_header = open(os.path.join(std_cpp_gen, "amc_generated.hpp"), encoding="utf-8").read()
        check("abix_Metrics_divide_result" in std_cpp_header and "struct Result" in std_cpp_header,
              "the C++ projection emits a typed Result wrapper")
        std_cpp_src = os.path.join(work, "std_cpp_consumer.cpp")
        open(std_cpp_src, "w", encoding="utf-8").write(
            '#include "std_api.hpp"\n'
            '#include "std_gen_cpp/amc_generated.hpp"\n'
            'int main() {\n'
            '    auto metrics = amc_generated::MetricsHandle::create();\n'
            '    if (!metrics) return 1;\n'
            '    auto ok = amc_generated::abix_Metrics_divide_result(metrics.native_handle(), 10, 2);\n'
            '    if (!ok || ok.value != 5) return 2;\n'
            '    auto err = amc_generated::abix_Metrics_divide_result(metrics.native_handle(), 10, 0);\n'
            '    if (err || err.error != 1) return 3;\n'
            '    return 0;\n'
            '}\n')
        std_cpp_bin = os.path.join(work, exe_name("std_cpp_consumer"))
        std_cpp_link = ["-L" + work, "-lmetrics"]
        if not IS_WINDOWS:
            std_cpp_link.append("-Wl,-rpath," + work)
        r = run([cxx, "-std=c++17", std_cpp_src, "-I" + work, *include_flags(source_root), *std_cpp_link,
                 "-o", std_cpp_bin])
        if check(r.returncode == 0, "the C++ typed Result consumer compiles", r.stderr[-400:]):
            r = run([std_cpp_bin], env=runtime_env(work))
            check(r.returncode == 0, "the C++ typed Result consumer ran", (r.stdout + r.stderr)[-400:])

    if zig and not IS_WINDOWS and not IS_MACOS:
        std_zig_gen = os.path.join(work, "std_gen_zig")
        os.makedirs(std_zig_gen, exist_ok=True)
        r = run([amc, "generate", std_abix, "-l", "zig", "-o", std_zig_gen])
        if check(r.returncode == 0, "amc generate -l zig for the ABIX-STD api", (r.stdout + r.stderr)[-400:]):
            std_zig_header = open(os.path.join(std_zig_gen, "amc_generated.zig"), encoding="utf-8").read()
            check("AmcResult" in std_zig_header and "abix_Metrics_divide_result" in std_zig_header,
                  "the Zig projection emits a typed Result wrapper")
            std_zig_src = os.path.join(work, "std_zig_consumer.zig")
            open(std_zig_src, "w", encoding="utf-8").write(
                'const abi = @import("std_gen_zig/amc_generated.zig");\n'
                'pub fn main() !void {\n'
                '    var metrics = abi.MetricsHandle.create() orelse return error.CreateFailed;\n'
                '    const ok = abi.abix_Metrics_divide_result(metrics.ptr, 10, 2);\n'
                '    switch (ok) { .ok => |v| { if (v != 5) return error.BadValue; }, .err => return error.Unexpected }\n'
                '    const err = abi.abix_Metrics_divide_result(metrics.ptr, 10, 0);\n'
                '    switch (err) { .ok => return error.Unexpected, .err => |e| { if (e != 1) return error.BadStatus; } }\n'
                '    metrics.deinit();\n'
                '}\n')
            std_zig_bin = os.path.join(work, exe_name("std_zig_consumer"))
            r = run([zig, "build-exe", "std_zig_consumer.zig", "-L.", "-lmetrics", "-lc",
                     "-rpath", work, "-femit-bin=" + std_zig_bin], cwd=work)
            if check(r.returncode == 0, "the Zig typed Result consumer compiles", (r.stdout + r.stderr)[-400:]):
                r = run([std_zig_bin], env=runtime_env(work))
                check(r.returncode == 0, "the Zig typed Result consumer ran", (r.stdout + r.stderr)[-400:])

    # Native↔ABIX-STD parameter adaptation: a declared [[adapt]] maps a native
    # STL parameter type onto an ABIX-STD boundary type, so the frontend exposes
    # a method that takes std::string / std::vector / std::span and generates
    # the marshalling adapter instead of refusing it.
    for name in ("native_api.hpp", "native_api.abic.toml", "rust_consumer_native.rs"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))
    r = run([amc, "build", "-c", os.path.join(work, "native_api.abic.toml"), "-B", os.path.join(work, "nat_out")])
    if not check(r.returncode == 0, "amc build for the native adaptation api", (r.stdout + r.stderr)[-400:]):
        return 1
    native_thunks = open(os.path.join(work, "native_thunks.cpp"), encoding="utf-8").read()
    check("abix_str_utf8" in native_thunks and "std::string(" in native_thunks,
          "a std::string parameter becomes a StringView with a generated adapter")
    check("abix_span_i32" in native_thunks and "std::vector<" in native_thunks,
          "a std::vector parameter becomes a SequenceView with a generated adapter")
    nat_module = module_path(work, "native")
    r = run([cxx, "-std=c++20", "-fPIC", "-shared", "-O2", os.path.join(work, "native_thunks.cpp"),
             *include_flags(source_root), "-o", nat_module] + module_flags(work, "native"))
    if not check(r.returncode == 0, "the native adaptation thunks compile", r.stderr[-400:]):
        return 1
    nat_gen = os.path.join(work, "nat_gen")
    os.makedirs(nat_gen, exist_ok=True)
    r = run([amc, "generate", os.path.join(work, "nat_out", "native_api.abix"), "-l", "rust", "-o", nat_gen])
    if not check(r.returncode == 0, "amc generate for the native adaptation api", (r.stdout + r.stderr)[-400:]):
        return 1
    nat_consumer_src = os.path.join(work, "rust_consumer_native.rs")
    text = open(nat_consumer_src, encoding="utf-8").read().replace("gen/amc_generated.rs", "nat_gen/amc_generated.rs")
    open(nat_consumer_src, "w", encoding="utf-8").write(text)
    nat_consumer = os.path.join(work, exe_name("rust_consumer_native"))
    r = run([rustc, "--edition", "2021", nat_consumer_src, "-o", nat_consumer]
            + rustc_link(work, "native"))
    if not check(r.returncode == 0, "rustc compiled the native adaptation consumer", r.stderr[-400:]):
        return 1
    r = run([nat_consumer], env=env)
    check(r.returncode == 0 and "expose native: OK" in r.stdout,
          "native string/vector/span parameters adapted onto ABIX-STD views", (r.stdout + r.stderr)[-400:])

    # Interface contracts: [[interface]] + [[implements]] project to a C++ header
    # carrying the identity, a Traits struct and an UniqueInterfaceProxy-derived class,
    # so a consumer can declare and call the interface without a private vtable.
    for name in ("shapes.hpp", "shapes.abic.toml"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))
    r = run([amc, "build", "-c", os.path.join(work, "shapes.abic.toml"), "-B", os.path.join(work, "shapes_out")])
    if check(r.returncode == 0, "amc build for the interface contract", (r.stdout + r.stderr)[-400:]):
        iface_header = os.path.join(work, "shapes_iface.hpp")
        r = run([amc, "generate", os.path.join(work, "shapes_out", "shapes.abix"), "-l", "cpp", "-o", iface_header])
        if check(r.returncode == 0, "amc generate -l cpp for the interface contract", (r.stdout + r.stderr)[-400:]):
            header = open(iface_header, encoding="utf-8").read()
            check("kShapeInterface" in header and "struct ShapeTraits" in header and "Shape_shape_area_fn" in header,
                  "the interface identity, Traits and method typedef were emitted")
            check("class Shape : public ::skl::abix::poly::UniqueInterfaceProxy<ShapeTraits>" in header,
                  "a handle interface projects to an UniqueInterfaceProxy-derived class")
            proxy_src = os.path.join(work, "shapes_proxy.cpp")
            open(proxy_src, "w", encoding="utf-8").write(
                '#include "shapes.hpp"\n'
                '#include "shapes_iface.hpp"\n'
                '#include <cstdint>\n'
                'struct FakeShape { std::int32_t area; };\n'
                'extern "C" std::int32_t fake_area(const void *object) {\n'
                '    return static_cast<const FakeShape *>(object)->area;\n'
                '}\n'
                'int main() {\n'
                '    static_assert(amc_generated::ShapeTraits::slots.shape_area == 0);\n'
                '    static_assert(amc_generated::ShapeTraits::signatures.size() == 1);\n'
                '    FakeShape object{42};\n'
                '    ::skl::abix::poly::InterfaceMethod methods[] = {\n'
                '        {amc_generated::ShapeTraits::signatures[0],\n'
                '         ::skl::abix::poly::interface_entry(&fake_area), 0, 0}};\n'
                '    amc_generated::Shape proxy;\n'
                '    if (proxy.bind_methods(amc_generated::ShapeTraits::id,\n'
                '            amc_generated::ShapeTraits::version, methods, 1, &object)\n'
                '        != ::skl::abix::poly::InterfaceStatus::ok)\n'
                '        return 1;\n'
                '    if (proxy.shape_area() != 42) return 2;\n'
                '    amc_generated::Shape wrong;\n'
                '    if (wrong.bind_methods(amc_generated::ShapeTraits::id,\n'
                '            amc_generated::ShapeTraits::version + 1, methods, 1, &object)\n'
                '        != ::skl::abix::poly::InterfaceStatus::version_mismatch)\n'
                '        return 3;\n'
                '    methods[0].signature = {};\n'
                '    amc_generated::Shape stale;\n'
                '    if (stale.bind_methods(amc_generated::ShapeTraits::id,\n'
                '            amc_generated::ShapeTraits::version, methods, 1, &object)\n'
                '        != ::skl::abix::poly::InterfaceStatus::signature_mismatch)\n'
                '        return 4;\n'
                '    return 0;\n'
                '}\n')
            proxy_bin = os.path.join(work, exe_name("shapes_proxy"))
            r = run([cxx, "-std=c++17", *include_flags(source_root), "-I" + work,
                     proxy_src, "-o", proxy_bin])
            if check(r.returncode == 0, "the generated interface proxy header compiles", r.stderr[-400:]):
                r = run([proxy_bin], env=env)
                check(r.returncode == 0, "the generated interface proxy header runs")

        # The same interface contract projects to Rust and Zig proxies with the
        # same bind-time id/version/signature checks.
        shapes_abix = os.path.join(work, "shapes_out", "shapes.abix")
        rust_proxy_gen = os.path.join(work, "shapes_gen_rust")
        os.makedirs(rust_proxy_gen, exist_ok=True)
        r = run([amc, "generate", shapes_abix, "-l", "rust", "-o", rust_proxy_gen])
        if check(r.returncode == 0, "amc generate -l rust for the interface proxy", (r.stdout + r.stderr)[-400:]):
            rust_proxy_src = os.path.join(work, "rust_interface_proxy.rs")
            open(rust_proxy_src, "w", encoding="utf-8").write(
                '#[path = "shapes_gen_rust/amc_generated.rs"]\n'
                'pub mod abix;\n'
                '\n'
                'mod skl {\n'
                '    pub use crate::abix;\n'
                '}\n'
                'use skl::abix::*;\n'
                'use core::ffi::c_void;\n'
                'unsafe extern "C" fn fake_area(object: *mut c_void) -> core::ffi::c_int {\n'
                '    unsafe { *(object as *mut i32) }\n'
                '}\n'
                'fn main() {\n'
                '    let mut value: i32 = 42;\n'
                '    let method = InterfaceMethodEntry {\n'
                '        signature_lo: ShapeProxy::SHAPE_AREA_SIGNATURE.0,\n'
                '        signature_hi: ShapeProxy::SHAPE_AREA_SIGNATURE.1,\n'
                '        entry: fake_area as usize,\n'
                '        slot: ShapeProxy::SHAPE_AREA_SLOT,\n'
                '        flags: 0,\n'
                '    };\n'
                '    let proxy = unsafe { ShapeProxy::bind(\n'
                '        (ShapeProxy::ID_LO, ShapeProxy::ID_HI), ShapeProxy::VERSION,\n'
                '        &mut value as *mut i32 as *mut c_void, &[method], None) };\n'
                '    let proxy = match proxy { Ok(p) => p, Err(_) => std::process::exit(1) };\n'
                '    if proxy.shape_area() != 42 { std::process::exit(2); }\n'
                '    let wrong = unsafe { ShapeProxy::bind(\n'
                '        (ShapeProxy::ID_LO, ShapeProxy::ID_HI), ShapeProxy::VERSION + 1,\n'
                '        &mut value as *mut i32 as *mut c_void, &[method], None) };\n'
                '    if wrong.err() != Some(BindError::VersionMismatch) { std::process::exit(3); }\n'
                '    let bad = InterfaceMethodEntry { signature_lo: 0, ..method };\n'
                '    let stale = unsafe { ShapeProxy::bind(\n'
                '        (ShapeProxy::ID_LO, ShapeProxy::ID_HI), ShapeProxy::VERSION,\n'
                '        &mut value as *mut i32 as *mut c_void, &[bad], None) };\n'
                '    if stale.err() != Some(BindError::SignatureMismatch) { std::process::exit(4); }\n'
                '    println!("rust interface proxy: OK");\n'
                '}\n')
            rust_proxy_bin = os.path.join(work, exe_name("rust_interface_proxy"))
            r = run([rustc, "--edition", "2021", rust_proxy_src, "-o", rust_proxy_bin])
            if check(r.returncode == 0, "the Rust interface proxy consumer compiles", r.stderr[-400:]):
                r = run([rust_proxy_bin], env=env)
                check(r.returncode == 0 and "rust interface proxy: OK" in r.stdout,
                      "the Rust interface proxy binds, calls and rejects stale contracts",
                      (r.stdout + r.stderr)[-400:])

        if zig and not IS_WINDOWS and not IS_MACOS:
            zig_proxy_gen = os.path.join(work, "shapes_gen_zig")
            os.makedirs(zig_proxy_gen, exist_ok=True)
            r = run([amc, "generate", shapes_abix, "-l", "zig", "-o", zig_proxy_gen])
            if check(r.returncode == 0, "amc generate -l zig for the interface proxy", (r.stdout + r.stderr)[-400:]):
                zig_proxy_src = os.path.join(work, "zig_interface_proxy.zig")
                open(zig_proxy_src, "w", encoding="utf-8").write(
                    'const std = @import("std");\n'
                    'const abi = @import("shapes_gen_zig/amc_generated.zig");\n'
                    'fn fake_area(object: ?*anyopaque) callconv(.c) c_int {\n'
                    '    const value: *i32 = @ptrCast(@alignCast(object.?));\n'
                    '    return value.*;\n'
                    '}\n'
                    'pub fn main() !void {\n'
                    '    var value: i32 = 42;\n'
                    '    const methods = [_]abi.InterfaceMethodEntry{.{\n'
                    '        .signature_lo = abi.ShapeProxy.shape_area_signature_lo,\n'
                    '        .signature_hi = abi.ShapeProxy.shape_area_signature_hi,\n'
                    '        .entry = @intFromPtr(&fake_area),\n'
                    '        .slot = abi.ShapeProxy.shape_area_slot,\n'
                    '        .flags = 0,\n'
                    '    }};\n'
                    '    const proxy = try abi.ShapeProxy.bind(abi.ShapeProxy.ID_LO, abi.ShapeProxy.ID_HI,\n'
                    '        abi.ShapeProxy.VERSION, &value, methods[0..], null);\n'
                    '    if (proxy.shape_area() != 42) return error.BadValue;\n'
                    '    const wrong = abi.ShapeProxy.bind(abi.ShapeProxy.ID_LO, abi.ShapeProxy.ID_HI,\n'
                    '        abi.ShapeProxy.VERSION + 1, &value, methods[0..], null);\n'
                    '    if (wrong != error.VersionMismatch) return error.ExpectedVersionMismatch;\n'
                    '    var stale = methods;\n'
                    '    stale[0].signature_lo = 0;\n'
                    '    const invalid = abi.ShapeProxy.bind(abi.ShapeProxy.ID_LO, abi.ShapeProxy.ID_HI,\n'
                    '        abi.ShapeProxy.VERSION, &value, stale[0..], null);\n'
                    '    if (invalid != error.SignatureMismatch) return error.ExpectedSignatureMismatch;\n'
                    '    std.debug.print("zig interface proxy: OK\\n", .{});\n'
                    '}\n')
                zig_proxy_bin = os.path.join(work, exe_name("zig_interface_proxy"))
                r = run([zig, "build-exe", "zig_interface_proxy.zig", "-lc",
                         "-femit-bin=" + zig_proxy_bin], cwd=work)
                if check(r.returncode == 0, "the Zig interface proxy consumer compiles", r.stderr[-400:]):
                    r = run([zig_proxy_bin], env=env)
                    check(r.returncode == 0 and "zig interface proxy: OK" in (r.stdout + r.stderr),
                          "the Zig interface proxy binds, calls and rejects stale contracts",
                          (r.stdout + r.stderr)[-400:])

        else:
            print("  [SKIP] Zig interface proxy consumer (zig not available or unsupported platform)")

    # Real-project comparison: expose a type from cmdly. Auto-expose covers its
    # representable surface; a std::string method is refused pending a contract.
    cmdly_root = os.environ.get("CMDLY_ROOT") or os.path.expanduser("~/Code/cmdly")
    cmdly_include = os.path.join(cmdly_root, "libcmdly", "include")
    cmdly_lib = None
    for candidate in ("build/Release/libcmdly", "build/libcmdly", "build/lib", "lib"):
        directory = os.path.join(cmdly_root, candidate)
        for name in ("libcmdly.so", "libcmdly.dylib", "libcmdly.dll", "cmdly.dll"):
            if os.path.isfile(os.path.join(directory, name)):
                cmdly_lib = directory
                break
        if cmdly_lib:
            break
    if not os.path.isdir(cmdly_include) or not cmdly_lib:
        print("  [SKIP] cmdly real-project comparison (set CMDLY_ROOT to enable)")
    else:
        # cmdly's headers include <fmt/...>; a Homebrew fmt is not on the default
        # search path, so ask pkg-config for its flags (same as cmdly_test.py).
        fmt_cflags = []
        fmt_probe = run(["pkg-config", "--cflags", "fmt"])
        if fmt_probe.returncode == 0:
            fmt_cflags = [flag.replace("\\", "/") for flag in fmt_probe.stdout.split()]
        fmt_toml = "".join(', "%s"' % flag for flag in fmt_cflags)
        # GNU ld-only option for transitive deps; the Apple and PE linkers
        # reject it.
        rpath_link = [] if (IS_MACOS or IS_WINDOWS) else [
            "-C", "link-arg=-Wl,-rpath-link," + cmdly_lib]
        key_header = os.path.join(cmdly_include, "cmdly", "key.h")
        key_config = os.path.join(work, "cmdly_key.abic.toml")
        open(key_config, "w", encoding="utf-8").write(f"""[package]
name = "cmdly_key"
version = "1.0"
[[import]]
language = "cpp"
flags = ["-std=c++17", "-I{toml_path(cmdly_include)}"{fmt_toml}]
files = ["{toml_path(key_header)}"]
symbols = ["cmdly::Key"]
[[expose]]
type = "cmdly::Key"
methods = ["code", "isPrintable"]
constructor = ["char"]
[thunks]
output = "cmdly_thunks.cpp"
[[export]]
output = "cmdly_key.abix"
""")
        r = run([amc, "build", "-c", key_config, "-B", os.path.join(work, "cmdly_out")])
        ok = check(r.returncode == 0, "cmdly::Key exposed automatically", (r.stdout + r.stderr)[-400:])
        if ok:
            key_module = module_path(work, "cmdlykey")
            key_link = ["-L" + cmdly_lib] + ([] if IS_WINDOWS else ["-Wl,-rpath," + cmdly_lib])
            r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", os.path.join(work, "cmdly_thunks.cpp"),
                     *include_flags(source_root), "-I" + cmdly_include, "-o", key_module]
                    + key_link + fmt_cflags + module_flags(work, "cmdlykey"))
            if check(r.returncode == 0, "cmdly thunks compile", r.stderr[-400:]):
                key_gen = os.path.join(work, "cmdly_gen")
                os.makedirs(key_gen, exist_ok=True)
                r = run([amc, "generate", os.path.join(work, "cmdly_out", "cmdly_key.abix"), "-l", "rust",
                         "-o", key_gen])
                check(r.returncode == 0, "cmdly capabilities project to Rust", (r.stdout + r.stderr)[-400:])
                key_src = os.path.join(work, "rust_consumer_key.rs")
                shutil.copy(os.path.join(fixture, "rust_consumer_key.rs"), key_src)
                key_text = open(key_src, encoding="utf-8").read().replace(
                    "gen/amc_generated.rs", "cmdly_gen/amc_generated.rs")
                open(key_src, "w", encoding="utf-8").write(key_text)
                kenv = runtime_env(work, [cmdly_lib])
                key_bin = os.path.join(work, exe_name("rust_consumer_key"))
                r = run([rustc, "--edition", "2021", key_src, "-o", key_bin]
                        + rustc_link(work, "cmdlykey", [cmdly_lib]) + rpath_link)
                if check(r.returncode == 0, "cmdly consumer compiles", r.stderr[-400:]):
                    r = run([key_bin], env=kenv)
                    check(r.returncode == 0 and "expose cmdly: OK" in r.stdout,
                          "Rust drove the real cmdly Key", (r.stdout + r.stderr)[-400:])

        # Without a declared [[adapt]] a std::string method is refused: an STL
        # type must not cross the boundary raw (ABI-SPEC §18 requires an
        # explicit mapping).
        ins_config = os.path.join(work, "cmdly_insert.abic.toml")
        ins_header = os.path.join(cmdly_include, "cmdly", "history.h")
        open(ins_config, "w", encoding="utf-8").write(f"""[package]
name = "cmdly_insert"
version = "1.0"
[[import]]
language = "cpp"
flags = ["-std=c++17", "-I{toml_path(cmdly_include)}"{fmt_toml}]
files = ["{toml_path(ins_header)}"]
symbols = ["cmdly::History"]
[[expose]]
type = "cmdly::History"
methods = ["insert"]
constructor = ["unsigned long"]
[thunks]
output = "insert_thunks.cpp"
[[export]]
output = "cmdly_insert.abix"
""")
        r = run([amc, "build", "-c", ins_config, "-B", os.path.join(work, "insert_out")])
        check(r.returncode != 0 and "needs a contract" in (r.stdout + r.stderr),
              "a std::string method is refused without an explicit adaptation", (r.stdout + r.stderr)[-400:])

        # With a declared [[adapt]] the same method is auto-exposed: the thunk
        # takes the StringView boundary type and marshals it back onto
        # std::string. A concrete derived type drives the inherited method.
        hist_config = os.path.join(work, "cmdly_history.abic.toml")
        open(hist_config, "w", encoding="utf-8").write(f"""[package]
name = "cmdly_history"
version = "1.0"
[[import]]
language = "cpp"
flags = ["-std=c++17", "-I{toml_path(cmdly_include)}"{fmt_toml}]
files = ["{toml_path(ins_header)}"]
symbols = ["cmdly::MemoryHistory"]
[[adapt]]
native = "std::string"
target = "abix_str_utf8"
[[expose]]
type = "cmdly::MemoryHistory"
methods = ["insert", "length"]
constructor = []
[thunks]
output = "history_thunks.cpp"
[[export]]
output = "cmdly_history.abix"
""")
        r = run([amc, "build", "-c", hist_config, "-B", os.path.join(work, "hist_out")])
        ok = check(r.returncode == 0, "cmdly::MemoryHistory::insert exposed after an adaptation",
                   (r.stdout + r.stderr)[-400:])
        if ok:
            history_thunks = open(os.path.join(work, "history_thunks.cpp"), encoding="utf-8").read()
            check("abix_str_utf8 line" in history_thunks and "std::string(line.data, line.len)" in history_thunks,
                  "the inherited insert takes a StringView and adapts it")
            history_module = module_path(work, "cmdlyhistory")
            history_link = ["-L" + cmdly_lib] + ([] if IS_WINDOWS else ["-Wl,-rpath," + cmdly_lib])
            r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", os.path.join(work, "history_thunks.cpp"),
                     *include_flags(source_root), "-I" + cmdly_include, "-o", history_module]
                    + history_link + fmt_cflags + module_flags(work, "cmdlyhistory"))
            if check(r.returncode == 0, "cmdly history thunks compile", r.stderr[-400:]):
                hist_gen = os.path.join(work, "hist_gen")
                os.makedirs(hist_gen, exist_ok=True)
                r = run([amc, "generate", os.path.join(work, "hist_out", "cmdly_history.abix"), "-l", "rust",
                         "-o", hist_gen])
                if check(r.returncode == 0, "cmdly history capabilities project to Rust", (r.stdout + r.stderr)[-400:]):
                    hist_src = os.path.join(work, "rust_consumer_history.rs")
                    shutil.copy(os.path.join(fixture, "rust_consumer_history.rs"), hist_src)
                    text = open(hist_src, encoding="utf-8").read().replace(
                        "gen/amc_generated.rs", "hist_gen/amc_generated.rs")
                    open(hist_src, "w", encoding="utf-8").write(text)
                    hist_bin = os.path.join(work, exe_name("rust_consumer_history"))
                    r = run([rustc, "--edition", "2021", hist_src, "-o", hist_bin]
                            + rustc_link(work, "cmdlyhistory", [cmdly_lib]) + rpath_link)
                    if check(r.returncode == 0, "cmdly history consumer compiles", r.stderr[-400:]):
                        hvenv = runtime_env(work, [cmdly_lib])
                        r = run([hist_bin], env=hvenv)
                        check(r.returncode == 0 and "expose history: OK" in r.stdout,
                              "Rust drove the real cmdly insert through a StringView", (r.stdout + r.stderr)[-400:])

    if KEEP_ARTIFACTS:
        print("  [KEEP] retained artifacts: " + work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    print("=== automatic capability generation: %d passed, %d failed ===" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
