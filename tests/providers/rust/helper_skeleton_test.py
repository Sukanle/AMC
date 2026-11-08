#!/usr/bin/env python3
"""Minimal `amc-rustc-helper` schema test.

Usage:
    helper_skeleton_test.py <amc_bin_dir> [source_root]

Builds the dependency-free helper with rustc, runs it against a fixture and
validates the `amc.rust.semantic/1` envelope. The helper intentionally emits an
empty type/function set in this first v0.4.0 increment; later revisions fill
the arrays without changing the envelope.
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
    keep = "--keep-artifacts" in sys.argv[1:]
    if not argv:
        print("usage: helper_skeleton_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    helper_source = source_root / "tools" / "rustc-helper" / "src" / "main.rs"
    fixture = source_root / "tests" / "fixtures" / "rust_demo.rs"
    fixture_config = source_root / "tests" / "fixtures" / "rust_demo.abic.toml"
    amc = bin_dir / exe("amc")
    rustc = shutil.which("rustc")
    if not rustc:
        print("  [SKIP] rustc is required for the helper test")
        return 0
    if not helper_source.is_file() or not fixture.is_file():
        print("  [FAIL] helper source or fixture missing", file=sys.stderr)
        return 1

    version = subprocess.run([rustc, "-vV"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace")
    sysroot = subprocess.run([rustc, "--print", "sysroot"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace")
    host = next((line.split(":", 1)[1].strip() for line in version.stdout.splitlines()
                 if line.startswith("host:")), "")
    sysroot_text = sysroot.stdout.strip() if sysroot.returncode == 0 else ""
    driver_lib = os.path.join(sysroot_text, "lib")
    driver_target_lib = os.path.join(sysroot_text, "lib", "rustlib", host, "lib")
    has_driver = any(name.startswith("librustc_driver") for name in os.listdir(driver_lib)) \
        if sysroot_text and os.path.isdir(driver_lib) else False
    if not has_driver:
        print("  [SKIP] rustc-dev component is not installed (librustc_driver missing)")
        return 0

    work = tempfile.mkdtemp(prefix="amc-rustc-helper.")
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
        helper = os.path.join(work, exe("amc-rustc-helper"))
        link = [
            "-L", driver_lib,
            "-L", driver_target_lib,
        ]
        if sys.platform != "win32":
            link += [
                "-C", "link-arg=-Wl,-rpath," + driver_lib,
                "-C", "link-arg=-Wl,-rpath," + driver_target_lib,
            ]
        build_env = dict(os.environ)
        build_env["RUSTC_BOOTSTRAP"] = "1"
        result = run([rustc, "--edition", "2021", str(helper_source), *link, "-o", helper],
                     env=build_env)
        if not check(result.returncode == 0, "helper compiled with rustc_driver", output(result)[-400:]):
            return 1

        version = run([helper, "--version"])
        check(version.returncode == 0 and "amc-rustc-helper" in version.stdout,
              "helper reports a version", output(version)[-200:])

        diagram = os.path.join(work, "semantic.json")
        result = run([helper, "--crate-name", "rust_demo", "--edition", "2021",
                      "--source", str(fixture), "--output", diagram])
        if not check(result.returncode == 0 and os.path.isfile(diagram),
                     "helper emitted a semantic document", output(result)[-400:]):
            return 1

        document = json.load(open(diagram, encoding="utf-8"))
        check(document.get("schema") == "amc.rust.semantic/1", "schema id")
        producer = document.get("producer", {})
        check(producer.get("kind") == "rustc-helper", "producer kind")
        check(bool(producer.get("rustc")), "producer rustc version")
        target = document.get("target", {})
        check(bool(target.get("triple")), "target triple")
        check(target.get("pointer_bits") in (32, 64), "target pointer width")
        check(target.get("endianness") in ("little", "big"), "target endianness")
        expected_clong = 32 if target.get("os") == "windows" else target.get("pointer_bits")
        check(target.get("c_long_bits") == expected_clong, "target c_long default")
        crate = document.get("crate", {})
        check(crate.get("name") == "rust_demo", "crate name")
        check(crate.get("edition") == "2021", "crate edition")
        for key in ("generics", "traits", "errors"):
            check(isinstance(document.get(key), list) and document[key] == [],
                  f"{key} remains an empty array in this increment")

        types = {item.get("name"): item for item in document.get("types", [])}
        check("RustRect" in types, "record type extracted")
        rect = types.get("RustRect", {})
        check(rect.get("size") == 24 and rect.get("align") == 8, "RustRect layout")
        check("rust_demo::RustRect" in rect.get("id", ""), "record identity is crate-qualified")
        fields = {field.get("name"): field for field in rect.get("fields", [])}
        check(fields.get("origin", {}).get("offset") == 0, "RustRect.origin offset")
        check(fields.get("width", {}).get("offset") == 8, "RustRect.width offset")
        check(fields.get("height", {}).get("offset") == 16, "RustRect.height offset")

        check("RustArray" in types, "packed-layout record extracted")
        array = types.get("RustArray", {})
        check(array.get("size") == 8 and array.get("align") == 2, "RustArray layout")
        array_fields = {field.get("name"): field for field in array.get("fields", [])}
        check(array_fields.get("values", {}).get("offset") == 2, "RustArray.values offset")

        check("RustTransparent" in types, "transparent record extracted")
        check("transparent" in types.get("RustTransparent", {}).get("repr", []), "transparent repr")
        check("u8" in types.get("RustSmallColor", {}).get("repr", []), "repr(u8) enum")

        functions = {item.get("name"): item for item in document.get("functions", [])}
        for name in ("rust_make_rect", "rust_rect_area", "rust_buffer_len", "rust_touch"):
            check(name in functions, f"function {name} extracted")
        check(functions.get("rust_rect_area", {}).get("abi") == "C", "function ABI is C")
        check("rust_demo::RustRect" in functions.get("rust_rect_area", {}).get("parameters", [{}])[0].get("type", ""),
              "function parameter uses the record identity")
        check(document.get("provenance", {}).get("source") == str(fixture), "provenance source")

        # stdout mode is part of the helper contract.
        result = run([helper, "--crate-name", "stdout_demo", "--source", str(fixture)])
        if check(result.returncode == 0, "helper supports stdout mode", output(result)[-200:]):
            stdout_document = json.loads(result.stdout)
            check(stdout_document.get("schema") == "amc.rust.semantic/1", "stdout schema id")

        # Unknown options fail closed instead of producing a partial document.
        result = run([helper, "--unknown"])
        check(result.returncode != 0, "unknown option is rejected", output(result)[-200:])

        # AMC integration: the Rust frontend fills missing target facts from the
        # helper. Remove [target] so a successful build proves the helper path.
        if amc.is_file():
            config = os.path.join(work, "helper_demo.abic.toml")
            text = fixture_config.read_text(encoding="utf-8")
            start = text.index("[target]")
            end = text.index("[[export]]")
            helper_toml = helper.replace("\\", "/")
            text = text[:start] + '[rust]\nhelper = "' + helper_toml + '"\n\n' + text[end:]
            with open(config, "w", encoding="utf-8") as handle:
                handle.write(text)
            shutil.copyfile(fixture, os.path.join(work, "rust_demo.rs"))
            out = os.path.join(work, "helper_out")
            result = run([str(amc), "build", "-c", config, "-B", out])
            check(result.returncode == 0 and os.path.isfile(os.path.join(out, "rust_demo.abix")),
                  "AMC fills target facts from the rust helper", output(result)[-400:])

            bad_config = os.path.join(work, "missing_helper.abic.toml")
            with open(bad_config, "w", encoding="utf-8") as handle:
                handle.write(text.replace(helper_toml, os.path.join(work, "missing-helper")))
            result = run([str(amc), "build", "-c", bad_config, "-B", os.path.join(work, "bad_out")])
            check(result.returncode != 0 and "rust helper" in output(result),
                  "missing helper fails closed when target facts are absent", output(result)[-400:])

            mismatch_config = os.path.join(work, "mismatch.abic.toml")
            mismatch_text = fixture_config.read_text(encoding="utf-8").replace(
                "pointer_bits = 64", "pointer_bits = 16", 1)
            mismatch_text = mismatch_text.replace(
                "[target]", '[rust]\nhelper = "' + helper_toml + '"\n\n[target]', 1)
            with open(mismatch_config, "w", encoding="utf-8") as handle:
                handle.write(mismatch_text)
            result = run([str(amc), "build", "-c", mismatch_config, "-B", os.path.join(work, "mismatch_out")])
            check(result.returncode != 0 and "does not match" in output(result),
                  "helper target mismatch is fatal", output(result)[-400:])

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
