#!/usr/bin/env python3
"""M7 / L3 interop fixture.

Builds one native library and one .abix, then drives the same contract through
C++, Rust and Zig consumers. Also checks target and stale-lowering rejection.
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


def roots(source_root):
    configured = os.environ.get("ABIX_INCLUDE_DIRS")
    if configured:
        return tuple(p for p in configured.split(os.pathsep) if p)
    return (
        os.path.join(source_root, "include"),
        os.path.join(source_root, "dependencies", "ABIX", "include"),
        os.path.join(source_root, "dependencies", "ABIX", "MICS", "include"),
        os.path.join(source_root, "dependencies", "ABIX", "MICS", "HLMD"),
    )


def include_flags(source_root):
    return ["-I" + p for p in roots(source_root)]


def module_path(work, base):
    if sys.platform == "darwin":
        return os.path.join(work, "lib" + base + ".dylib")
    if sys.platform == "win32":
        return os.path.join(work, base + ".dll")
    return os.path.join(work, "lib" + base + ".so")


def rust_link(work, base):
    args = ["-L", "native=" + work, "-l", "dylib=" + base]
    if sys.platform != "win32":
        args += ["-C", "link-arg=-Wl,-rpath," + work]
    return args


def env_for(work):
    env = dict(os.environ)
    if sys.platform == "win32":
        env["PATH"] = work + os.pathsep + env.get("PATH", "")
    else:
        env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    return env


def main():
    argv = [a for a in sys.argv[1:] if a != "--keep-artifacts"]
    if len(argv) < 2:
        print("usage: m7_test.py <amc_bin_dir> <source_root>", file=sys.stderr)
        return 2
    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve()
    amc = str(bin_dir / exe("amc"))
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")
    zig = shutil.which("zig")
    if not cxx or not rustc:
        print("  [SKIP] m7 interop needs a C++ compiler and rustc")
        return 0

    work = tempfile.mkdtemp(prefix="amc-m7.")
    passed = failed = 0

    def check(ok, message, detail=""):
        nonlocal passed, failed
        if ok:
            passed += 1
            print("  [PASS] " + message)
        else:
            failed += 1
            print("  [FAIL] " + message + ((" :: " + detail) if detail else ""))

    def run(args, **kwargs):
        return subprocess.run(args, capture_output=True, text=True, **kwargs)

    try:
        fixture = os.path.dirname(os.path.abspath(__file__))
        for name in ("api.hpp", "api.cpp", "api.abic.toml", "rust_consumer.rs",
                     "cpp_consumer.cpp", "zig_consumer.zig"):
            shutil.copyfile(os.path.join(fixture, name), os.path.join(work, name))
        lib = module_path(work, "m7")
        r = run([cxx, "-std=c++17", "-shared", "-fPIC", "-O2", os.path.join(work, "api.cpp"),
                 "-o", lib])
        check(r.returncode == 0, "native M7 library built", r.stderr[-400:])

        config = os.path.join(work, "api.abic.toml")
        text = open(config, encoding="utf-8").read().replace(
            '"-std=c++17"', '"-std=c++17", ' + ", ".join('"%s"' % flag.replace("\\", "/") for flag in include_flags(str(source_root))))
        open(config, "w", encoding="utf-8").write(text)
        out = os.path.join(work, "abix")
        r = run([amc, "build", "-c", config, "-B", out])
        artifact = os.path.join(out, "m7.abix")
        check(r.returncode == 0 and os.path.isfile(artifact), "M7 artifact built", (r.stdout + r.stderr)[-400:])

        gen_rust = os.path.join(work, "gen")
        r = run([amc, "generate", artifact, "-l", "rust", "-o", gen_rust])
        check(r.returncode == 0, "M7 Rust projection generated", (r.stdout + r.stderr)[-400:])
        rust_src = os.path.join(work, "rust_consumer.rs")
        rust_bin = os.path.join(work, exe("rust_consumer"))
        r = run([rustc, "--edition", "2021", rust_src, "-o", rust_bin] + rust_link(work, "m7"))
        if check(r.returncode == 0, "M7 Rust consumer compiled", r.stderr[-400:]):
            r = run([rust_bin], env=env_for(work))
            check(r.returncode == 0 and "m7 rust: OK" in r.stdout,
                  "M7 Rust consumer ran", (r.stdout + r.stderr)[-400:])

        gen_cpp = os.path.join(work, "gen_cpp")
        r = run([amc, "generate", artifact, "-l", "cpp", "-o", gen_cpp])
        check(r.returncode == 0, "M7 C++ projection generated", (r.stdout + r.stderr)[-400:])
        cpp_bin = os.path.join(work, exe("cpp_consumer"))
        link = []
        if sys.platform != "win32":
            link += ["-L", work, "-l", "m7", "-Wl,-rpath," + work]
        r = run([cxx, "-std=c++17", "-O2", os.path.join(work, "cpp_consumer.cpp"),
                 "-I", work, "-I", gen_cpp, *include_flags(str(source_root)), *link, "-o", cpp_bin])
        if check(r.returncode == 0, "M7 C++ consumer compiled", r.stderr[-400:]):
            r = run([cpp_bin], env=env_for(work))
            check(r.returncode == 0 and "m7 cpp: OK" in r.stdout,
                  "M7 C++ consumer ran", (r.stdout + r.stderr)[-400:])

        if zig:
            gen_zig = os.path.join(work, "gen_zig")
            r = run([amc, "generate", artifact, "-l", "zig", "-o", gen_zig])
            check(r.returncode == 0, "M7 Zig projection generated", (r.stdout + r.stderr)[-400:])
            zig_bin = os.path.join(work, exe("zig_consumer"))
            r = run([zig, "build-exe", "zig_consumer.zig", "-lc", "-L", work, "-lm7",
                     "-rpath", work, "-femit-bin=" + zig_bin], cwd=work)
            if check(r.returncode == 0, "M7 Zig consumer compiled", r.stderr[-400:]):
                r = run([zig_bin], env=env_for(work))
                check(r.returncode == 0 and "m7 zig: OK" in (r.stdout + r.stderr),
                      "M7 Zig consumer ran", (r.stdout + r.stderr)[-400:])
        else:
            print("  [SKIP] Zig consumer (zig not available)")

        # Negative: target mismatch must be rejected by the artifact check.
        r = run([amc, "target", artifact, "--expect", "arch=2"])
        check(r.returncode != 0, "M7 target mismatch is rejected", (r.stdout + r.stderr)[-400:])

        # Negative: a lowering sidecar from another build must not be projected.
        stale_abix = os.path.join(work, "stale.abix")
        shutil.copyfile(artifact, stale_abix)
        lowering_path = Path(artifact + ".lowering")
        if lowering_path.is_file():
            data = json.loads(lowering_path.read_text(encoding="utf-8"))
            if data.get("types"):
                data["types"][0]["abi"] = "0" * 32
                Path(str(stale_abix) + ".lowering").write_text(json.dumps(data), encoding="utf-8")
                r = run([amc, "generate", stale_abix, "-l", "rust", "-o", os.path.join(work, "stale_gen")])
                check(r.returncode != 0 and "lowering sidecar references" in (r.stdout + r.stderr),
                      "M7 stale lowering is rejected", (r.stdout + r.stderr)[-400:])
            else:
                print("  [SKIP] stale lowering (artifact has no lowered types)")

        print(f"=== M7 / L3 interop: {passed} passed, {failed} failed ===")
        return 1 if failed else 0
    finally:
        if os.environ.get("KEEP_ARTIFACTS", "") == "1" or "--keep-artifacts" in sys.argv:
            print("  [KEEP] " + work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
