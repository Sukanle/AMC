#!/usr/bin/env python3
"""Lease C ABI interop fixture.

Builds the AMC-side Host registry, a provider module, and C++/Rust/Zig
consumers. The consumers exercise acquire -> create -> destroy -> release,
draining/busy, registry CLOSING/destroy and the Host function table.
"""

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
    flags = []
    amc_include = os.path.join(source_root, "include")
    if os.path.isdir(amc_include):
        flags.append("-I" + amc_include)
    return flags + ["-I" + p for p in roots(source_root)]


def module_path(work, base):
    if sys.platform == "darwin":
        return os.path.join(work, "lib" + base + ".dylib")
    if sys.platform == "win32":
        return os.path.join(work, base + ".dll")
    return os.path.join(work, "lib" + base + ".so")


def module_flags(work, base):
    if sys.platform == "win32":
        return ["-Wl,--export-all-symbols", "-Wl,--out-implib," + os.path.join(work, "lib" + base + ".dll.a")]
    return []


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
    keep = "--keep-artifacts" in sys.argv[1:] or os.environ.get("KEEP_ARTIFACTS", "") == "1"
    if len(argv) < 2:
        print("usage: lease_test.py <amc_bin_dir> <source_root>", file=sys.stderr)
        return 2
    source_root = Path(argv[1]).resolve()
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")
    zig = shutil.which("zig")
    if not cxx or not rustc:
        print("  [SKIP] lease interop needs a C++ compiler and rustc")
        return 0

    work = tempfile.mkdtemp(prefix="amc-lease.")
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

    def failure_detail(r):
        return (r.stderr or "")[-400:]

    try:
        fixture = os.path.dirname(os.path.abspath(__file__))
        for name in ("host_lease.hpp", "host_lease_test.cpp",
                     "lease_bridge.hpp", "lease_bridge.cpp", "provider.cpp",
                     "rust_consumer.rs", "rust_takeover.rs", "zig_consumer.zig"):
            shutil.copyfile(os.path.join(fixture, name), os.path.join(work, name))

        # The public Host registry lives in libabix-format now; the interop
        # fixture links that archive instead of carrying a private copy.
        build_dir = Path(argv[0]).resolve().parent
        runtime_archive = None
        for candidate in (build_dir / "libabix-format.a", build_dir / "libabix-format.lib"):
            if candidate.is_file():
                runtime_archive = str(candidate)
                break
        if runtime_archive is None:
            print("  [FAIL] libabix-format archive not found next to the bin dir", file=sys.stderr)
            return 1

        includes = include_flags(str(source_root))
        provider = module_path(work, "lease_provider")
        zig_src = os.path.join(work, "zig_consumer.zig")
        zig_text = open(zig_src, encoding="utf-8").read().replace(
            "LEASE_MODULE_PATH", provider.replace("\\", "/"))
        open(zig_src, "w", encoding="utf-8").write(zig_text)
        bridge = module_path(work, "lease_bridge")

        r = run([cxx, "-std=c++17", "-shared", "-fPIC", "-O2", os.path.join(work, "provider.cpp"),
                 "-o", provider] + module_flags(work, "lease_provider"))
        check(r.returncode == 0, "provider module built", failure_detail(r))

        provider_b = module_path(work, "lease_provider_b")
        r = run([cxx, "-std=c++17", "-shared", "-fPIC", "-O2", os.path.join(work, "provider.cpp"),
                 "-o", provider_b] + module_flags(work, "lease_provider_b"))
        check(r.returncode == 0, "second provider module built", failure_detail(r))

        bridge_link = [] if sys.platform != "linux" else ["-ldl"]
        r = run([cxx, "-std=c++17", "-shared", "-fPIC", "-O2", "-pthread",
                 os.path.join(work, "lease_bridge.cpp"),
                 "-I", work, *includes, "-o", bridge, *bridge_link,
                 *module_flags(work, "lease_bridge"), runtime_archive])
        check(r.returncode == 0, "Host lease bridge built", failure_detail(r))

        takeover = module_path(work, "rust_takeover")
        r = run([rustc, "--edition", "2021", "--crate-type", "cdylib",
                 os.path.join(work, "rust_takeover.rs"), "-o", takeover]
                + rust_link(work, "lease_bridge"))
        takeover_built = check(r.returncode == 0, "Rust lease takeover built", failure_detail(r))

        host_test = os.path.join(work, exe("host_lease_test"))
        r = run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-pthread",
                 os.path.join(work, "host_lease_test.cpp"),
                 "-I", work, *includes, "-o", host_test, runtime_archive])
        if check(r.returncode == 0, "Host lease contract test compiled", failure_detail(r)):
            r = run([host_test])
            check(r.returncode == 0 and "lease host: OK" in output(r),
                  "Host lease contract test passed", output(r)[-400:])

        rust_bin = os.path.join(work, exe("rust_consumer"))
        r = run([rustc, "--edition", "2021", os.path.join(work, "rust_consumer.rs"),
                 "-o", rust_bin] + rust_link(work, "lease_bridge"))
        if check(r.returncode == 0, "Rust lease consumer compiled", failure_detail(r)):
            r = run([rust_bin, provider, provider_b], env=env_for(work))
            check(r.returncode == 0 and "lease rust: OK" in output(r),
                  "Rust lease consumer ran", output(r)[-400:])

        if zig:
            zig_bin = os.path.join(work, exe("zig_consumer"))
            # Link the bridge directly. Zig 0.17 rejects a C++ shared library
            # that carries an `ld-linux` NEEDED entry (the ABIX loader uses a
            # thread-local diagnostic, which pulls TLS into the bridge).
            if sys.platform == "win32":
                zig_link = ["-L", work, "-llelease_bridge"]
                if takeover_built:
                    zig_link += [takeover]
            else:
                zig_link = [bridge]
                if takeover_built:
                    zig_link.append(takeover)
            r = run([zig, "build-exe", zig_src, "-lc",
                     *zig_link, "-rpath", work, "-femit-bin=" + zig_bin], cwd=work)
            if check(r.returncode == 0, "Zig lease consumer compiled", failure_detail(r)):
                r = run([zig_bin, provider], env=env_for(work))
                check(r.returncode == 0 and "lease zig: OK" in output(r),
                      "Zig lease consumer ran", output(r)[-400:])
        else:
            print("  [SKIP] Zig lease consumer (zig not available)")

        if keep:
            print("  kept artifacts in " + work)
            return 0 if failed == 0 else 1
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
