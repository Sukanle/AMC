#!/usr/bin/env python3
"""Minimal `amc-rustc-helper` schema test.

Usage:
    helper_skeleton_test.py <amc_bin_dir> [source_root]

Builds the dependency-free helper with rustc, runs it against the demo and
semantic fixtures, and validates the `amc.rust.semantic/1` envelope: types,
functions, generics, traits, errors, cargo metadata and the layout probe. It
also checks that AMC imports the helper types/functions into ABIX/Semantic IR.
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
    helper_manifest = source_root / "tools" / "rustc-helper" / "Cargo.toml"
    helper_target = source_root / "tools" / "rustc-helper" / "target"
    fixture = source_root / "tests" / "fixtures" / "rust_demo.rs"
    fixture_config = source_root / "tests" / "fixtures" / "rust_demo.abic.toml"
    amc = bin_dir / exe("amc")
    rustc = shutil.which("rustc")
    cargo = shutil.which("cargo")
    if not rustc or not cargo:
        print("  [SKIP] rustc and cargo are required for the helper test")
        return 0
    if not helper_manifest.is_file() or not fixture.is_file():
        print("  [FAIL] helper manifest or fixture missing", file=sys.stderr)
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
        helper = os.path.join(helper_target, "release", exe("amc-rustc-helper"))
        build_env = dict(os.environ)
        build_env["RUSTC_BOOTSTRAP"] = "1"
        result = run([cargo, "build", "--release", "--locked",
                      "--manifest-path", str(helper_manifest),
                      "--target-dir", str(helper_target)],
                     cwd=str(helper_manifest.parent), env=build_env)
        if not check(result.returncode == 0 and os.path.isfile(helper),
                     "helper compiled with cargo and rustc_driver", output(result)[-600:]):
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
                  f"{key} is empty for the demo fixture")

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

        # Advanced semantics: nested modules, impl methods, generics, traits and
        # Result error types.
        semantic_fixture = source_root / "tests" / "fixtures" / "rust_semantic.rs"
        semantic_json = os.path.join(work, "semantic_advanced.json")
        result = run([helper, "--crate-name", "rust_semantic", "--edition", "2021",
                      "--source", str(semantic_fixture), "--output", semantic_json])
        if check(result.returncode == 0 and os.path.isfile(semantic_json),
                 "helper extracted the advanced fixture", output(result)[-400:]):
            advanced = json.load(open(semantic_json, encoding="utf-8"))
            advanced_types = {item.get("name"): item for item in advanced.get("types", [])}
            check("nested::NestedPoint" in advanced_types, "nested module type extracted")
            check("Wrapper<i32>" in advanced_types, "concrete generic instance extracted")
            check(advanced_types.get("Wrapper<i32>", {}).get("size") == 4,
                  "generic instance layout")
            check(advanced_types.get("Wrapper", {}).get("generic") is True,
                  "generic declaration is marked")
            check("WrapperI32" in advanced_types, "generic alias extracted")

            advanced_functions = {item.get("name"): item for item in advanced.get("functions", [])}
            nested_sum = advanced_functions.get("nested_sum", {})
            check(nested_sum.get("kind") == "associated", "impl associated function kind")
            check("nested::NestedPoint" in nested_sum.get("owner", ""),
                  "impl method owner is recorded")
            check("rust_try_fallible" in advanced_functions, "fallible function extracted")

            generics = {item.get("name"): item for item in advanced.get("generics", [])}
            wrapper = generics.get("Wrapper", {})
            check(any(parameter.get("name") == "T" for parameter in wrapper.get("parameters", [])),
                  "generic type parameter extracted")
            check(any(name.endswith("Wrapper<i32>") for name in wrapper.get("instances", [])),
                  "generic instance is linked to its declaration")

            traits = advanced.get("traits", [])
            trait_decl = next((item for item in traits if item.get("kind") == "trait"), {})
            check(trait_decl.get("name") == "Shape", "trait declaration extracted")
            check({method.get("name") for method in trait_decl.get("methods", [])} == {"area", "perimeter"},
                  "trait methods extracted")
            check(any(item.get("kind") == "impl" and item.get("self_type", "").endswith("::Circle")
                      for item in traits), "trait impl extracted")
            check(any(item.get("kind") == "impl"
                      and item.get("self_type", "").endswith("::nested::NestedPoint")
                      for item in traits), "inherent impl extracted")

            errors = {item.get("name"): item for item in advanced.get("errors", [])}
            failure = errors.get("RustFailure", {})
            check(failure.get("kind") == "enum", "error enum extracted")
            check({variant.get("name") for variant in failure.get("variants", [])}
                  == {"None", "InvalidInput", "Overflow"}, "error variants extracted")
            check(any("rust_try_fallible" in name for name in failure.get("functions", [])),
                  "error is linked to its functions")

            # Cross-validate the helper layout with a rustc `size_of` /
            # `align_of` / `offset_of!` probe, independent of AMC's parser.
            probe_source = Path(work) / "semantic_probe.rs"
            probe_exe = Path(work) / exe("semantic_probe")
            cases = {
                "Circle": ["radius"],
                "nested::NestedPoint": ["x", "y"],
                "Wrapper<i32>": ["value"],
            }
            lines = [
                "use std::mem::{align_of, offset_of, size_of};",
                f'#[path = r"{semantic_fixture}"]',
                "mod fixture;",
                "fn main() {",
            ]
            for name, fields in cases.items():
                lines.append(f'    println!("{name}.size {{}}", size_of::<fixture::{name}>());')
                lines.append(f'    println!("{name}.align {{}}", align_of::<fixture::{name}>());')
                for field in fields:
                    lines.append(
                        f'    println!("{name}.{field}.offset {{}}", offset_of!(fixture::{name}, {field}));'
                    )
            lines.append("}")
            probe_source.write_text("\n".join(lines) + "\n", encoding="utf-8")
            probe_build = run([rustc, "--edition", "2021", str(probe_source), "-o", str(probe_exe)])
            if check(probe_build.returncode == 0, "layout probe compiled",
                     output(probe_build)[-400:]):
                probe_run = run([str(probe_exe)])
                facts = {}
                for line in probe_run.stdout.splitlines():
                    parts = line.split()
                    if len(parts) == 2 and parts[1].isdigit():
                        facts[parts[0]] = int(parts[1])
                probe_ok = True
                for name, fields in cases.items():
                    helper_type = advanced_types.get(name, {})
                    probe_ok &= helper_type.get("size") == facts.get(f"{name}.size")
                    probe_ok &= helper_type.get("align") == facts.get(f"{name}.align")
                    offsets = {field.get("name"): field.get("offset")
                               for field in helper_type.get("fields", [])}
                    for field in fields:
                        probe_ok &= offsets.get(field) == facts.get(f"{name}.{field}.offset")
                check(probe_ok, "helper layout matches the rustc probe")

        # Cargo metadata fills workspace facts when a manifest is available.
        cargo_dir = Path(work) / "cargo-demo"
        (cargo_dir / "src").mkdir(parents=True)
        (cargo_dir / "Cargo.toml").write_text(
            "[package]\nname = \"cargo-demo\"\nversion = \"0.2.0\"\nedition = \"2021\"\n\n"
            "[features]\ndefault = [\"fast\"]\nfast = []\nslow = []\n",
            encoding="utf-8")
        cargo_source = cargo_dir / "src" / "lib.rs"
        cargo_source.write_text(
            '#[no_mangle]\npub extern "C" fn cargo_answer() -> i32 { 42 }\n',
            encoding="utf-8")
        cargo_json = os.path.join(work, "cargo.json")
        result = run([helper, "--crate-name", "cargo_demo", "--edition", "2021",
                      "--source", str(cargo_source), "--manifest-path", str(cargo_dir / "Cargo.toml"),
                      "--features", "slow", "--output", cargo_json])
        if check(result.returncode == 0 and os.path.isfile(cargo_json),
                 "helper reads cargo metadata", output(result)[-400:]):
            cargo_crate = json.load(open(cargo_json, encoding="utf-8")).get("crate", {})
            check(cargo_crate.get("name") == "cargo-demo", "cargo package name")
            check(cargo_crate.get("edition") == "2021", "cargo edition")
            check(set(cargo_crate.get("features", [])) == {"default", "fast", "slow"},
                  "cargo features are recorded")

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

            # Helper-only syntax: the source parser rejects the generic alias,
            # but the helper supplies the concrete layout and the build must
            # still import nested modules, generic instances and impl methods.
            import_dir = Path(work) / "helper_import"
            import_dir.mkdir()
            shutil.copyfile(semantic_fixture, import_dir / "rust_semantic.rs")
            import_config = import_dir / "helper_import.abic.toml"
            import_config.write_text(
                "[package]\nname = \"rust_semantic\"\nversion = \"0.1\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"rust_semantic.rs\"]\n"
                "symbols = [\"Circle\", \"NestedPoint\", \"WrapperI32\", \"nested_sum\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\n\n"
                "[[export]]\noutput = \"rust_semantic.abix\"\n",
                encoding="utf-8")
            import_out = import_dir / "out"
            result = run([str(amc), "build", "-c", str(import_config), "-B", str(import_out)])
            import_artifact = import_out / "rust_semantic.abix"
            if check(result.returncode == 0 and import_artifact.is_file(),
                     "AMC imports helper semantic entities", output(result)[-400:]):
                check("layout differs from the source parser" not in output(result),
                      "AMC cross-validates helper and parser layouts")
                semantic_path = Path(str(import_artifact) + ".semantic")
                check(semantic_path.is_file(), "AMC writes the imported semantic sidecar")
                if semantic_path.is_file():
                    semantic = json.loads(semantic_path.read_text(encoding="utf-8"))
                    names = {item.get("name") for item in semantic.get("types", [])}
                    check({"NestedPoint", "WrapperI32", "Wrapper<i32>"} <= names,
                          "helper types reached Semantic IR")
                    check(bool(semantic.get("functions")), "helper functions reached Semantic IR")
                query = run([str(amc), "query", str(import_artifact), "--type", "WrapperI32",
                             "--format", "json", "--layout"])
                if check(query.returncode == 0, "AMC queries the imported alias", output(query)[-300:]):
                    match = json.loads(query.stdout).get("matches", [{}])[0]
                    check(match.get("size") == 4 and match.get("align") == 4,
                          "imported alias keeps the helper layout")
                query = run([str(amc), "query", str(import_artifact), "--type", "NestedPoint",
                             "--format", "json", "--layout"])
                if check(query.returncode == 0, "AMC queries the imported nested record", output(query)[-300:]):
                    match = json.loads(query.stdout).get("matches", [{}])[0]
                    check(match.get("size") == 8 and match.get("align") == 4,
                          "imported nested record keeps the helper layout")

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
