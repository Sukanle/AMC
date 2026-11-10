#!/usr/bin/env python3
"""Rust -> AMC -> OpenCV bridge test.

Usage:
    opencv_bridge_test.py <amc_bin_dir> [source_root]

Builds a small C++ shim over OpenCV's C++ API, a Rust provider that calls the
shim, and a `#![deny(unsafe_code)]` consumer that uses the AMC-generated safe
facade. The test is skipped when OpenCV or pkg-config is unavailable.
"""

import os
import shlex
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
    return os.path.join(work, "lib" + name + ".so")


def opencv_cflags():
    if not shutil.which("pkg-config"):
        return None
    for package in ("opencv5", "opencv4", "opencv"):
        result = subprocess.run(
            ["pkg-config", "--cflags", package],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if result.returncode == 0:
            return package, shlex.split(result.stdout)
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: opencv_bridge_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else bin_dir.parent.parent
    amc = bin_dir / exe("amc")
    if sys.platform == "win32":
        print("  [SKIP] the OpenCV bridge test currently covers POSIX only")
        return 0
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")
    flags = opencv_cflags()
    if not cxx or not rustc:
        print("  [SKIP] C++ and rustc are required for the OpenCV bridge test")
        return 0
    if flags is None:
        print("  [SKIP] OpenCV is not discoverable through pkg-config")
        return 0
    package, cflags = flags

    fixture = Path(__file__).resolve().parent
    work = tempfile.mkdtemp(prefix="amc-opencv.")
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
        return subprocess.run(
            args, capture_output=True, text=True, encoding="utf-8", errors="replace", **kwargs
        )

    def output(result):
        return (result.stdout or "") + (result.stderr or "")

    try:
        for name in (
            "opencv_bridge.rs",
            "opencv_shim_ffi.rs",
            "opencv_shim.cpp",
            "opencv_bridge.abic.toml",
            "opencv_consumer.rs",
        ):
            shutil.copyfile(fixture / name, os.path.join(work, name))

        config = os.path.join(work, "opencv_bridge.abic.toml")
        out = os.path.join(work, "out")
        artifact = os.path.join(out, "opencv_bridge.abix")
        result = run([str(amc), "build", "-c", config, "-B", out])
        if not check(
            result.returncode == 0 and os.path.isfile(artifact),
            f"OpenCV bridge artifact built with {package}",
            output(result)[-600:],
        ):
            return 1

        gen = os.path.join(work, "gen")
        result = run([str(amc), "generate", artifact, "-l", "rust", "-o", gen])
        projection = os.path.join(gen, "amc_generated.rs")
        if check(
            result.returncode == 0 and os.path.isfile(projection),
            "OpenCV bridge Rust projection generated",
            output(result)[-500:],
        ):
            text = Path(projection).read_text(encoding="utf-8")
            check(
                "pub fn opencv_mat_set(mat: &mut OpencvMat" in text
                and "pub fn opencv_mat_sum(mat: &OpencvMat) ->" in text
                and "pub fn as_mut(&mut self) -> Option<&mut OpencvMat>" in text,
                "borrowed Mat accessors and safe facade are generated",
            )

        shim = module_path(work, "opencv_shim")
        result = run(
            [cxx, "-std=c++17", "-shared", "-fPIC", os.path.join(work, "opencv_shim.cpp"), "-o", shim]
            + cflags
            + ["-lopencv_core"]
        )
        if not check(result.returncode == 0, "OpenCV C++ shim built", output(result)[-600:]):
            return 1

        provider = module_path(work, "opencv_bridge")
        result = run(
            [rustc, "--edition", "2021", "--crate-type", "cdylib", os.path.join(work, "opencv_bridge.rs"),
             "-o", provider, "-L", "native=" + work, "-l", "dylib=opencv_shim",
             "-C", "link-arg=-Wl,-rpath," + work]
        )
        if not check(result.returncode == 0, "Rust OpenCV bridge compiled", output(result)[-600:]):
            return 1

        consumer = os.path.join(work, exe("opencv_consumer"))
        result = run(
            [rustc, "--edition", "2021", os.path.join(work, "opencv_consumer.rs"), "-o", consumer,
             "-L", "native=" + work, "-l", "dylib=opencv_bridge",
             "-C", "link-arg=-Wl,-rpath," + work]
        )
        if check(result.returncode == 0, "OpenCV bridge consumer compiled without unsafe",
                 output(result)[-600:]):
            env = dict(os.environ)
            env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
            env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
            result = run([consumer], env=env)
            check(result.returncode == 0 and "opencv rust bridge: OK" in output(result),
                  "Rust consumer drives OpenCV through the AMC safe facade", output(result)[-600:])

        if failed:
            return 1
        print(f"opencv bridge: {passed} passed, 0 failed")
        return 0
    finally:
        if os.environ.get("KEEP_ARTIFACTS", "") == "1":
            print("  [KEEP] retained artifacts: " + work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
