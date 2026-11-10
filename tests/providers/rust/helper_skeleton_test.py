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
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def exe(name):
    return name + ".exe" if sys.platform == "win32" else name


def abix_error_codes(artifact: Path) -> set[int]:
    """Read section 30 (error codes) from an ABIX v4 artifact."""
    data = artifact.read_bytes()
    if len(data) < 20 or data[:4] != b"ABIX":
        return set()
    _, _, _, section_count, directory = struct.unpack_from("<HHIII", data, 4)
    sections = {}
    for index in range(section_count):
        section_id, offset, length, count, entry_size, _ = struct.unpack_from(
            "<IIIIII", data, directory + index * 24)
        sections[section_id] = (offset, length, count, entry_size)
    if 30 not in sections:
        return set()
    offset, _length, count, entry_size = sections[30]
    return {
        struct.unpack_from("<i", data, offset + index * entry_size + 24)[0]
        for index in range(count)
    }


def main():
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    keep = "--keep-artifacts" in sys.argv[1:] or os.environ.get("KEEP_ARTIFACTS", "") == "1"
    if not argv:
        print("usage: helper_skeleton_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    helper_manifest = source_root / "tools" / "rustc-helper" / "Cargo.toml"
    helper_target = source_root / "tools" / "rustc-helper" / "target"
    fixture = source_root / "tests" / "fixtures" / "rust" / "demo" / "rust_demo.rs"
    fixture_config = source_root / "tests" / "fixtures" / "rust" / "demo" / "rust_demo.abic.toml"
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
    sysroot_text = sysroot.stdout.strip() if sysroot.returncode == 0 else ""
    driver_lib = os.path.join(sysroot_text, "lib")
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
        semantic_fixture = source_root / "tests" / "fixtures" / "rust" / "semantics" / "rust_semantic.rs"
        semantic_json = os.path.join(work, "semantic_advanced.json")
        result = run([helper, "--crate-name", "rust_semantic", "--edition", "2021",
                      "--source", str(semantic_fixture), "--instantiate", "Wrapper=u64",
                      "--instantiate", "Buffer=4", "--instantiate", "Wrapper=NestedPoint",
                      "--output", semantic_json])
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
            check("Wrapper<u64>" in advanced_types, "explicit generic instance extracted")
            buffer = generics.get("Buffer", {})
            check(any(parameter.get("name") == "N" and parameter.get("kind") == "const"
                      for parameter in buffer.get("parameters", [])),
                  "const generic parameter extracted")
            check(any(name.endswith("Buffer<4>") for name in buffer.get("instances", [])),
                  "const generic instance is linked to its declaration")
            check(advanced_types.get("Wrapper<u64>", {}).get("size") == 8,
                  "explicit generic instance layout")
            check("Buffer<4>" in advanced_types, "const generic instance extracted")
            check(advanced_types.get("Buffer<4>", {}).get("size") == 4
                  and advanced_types.get("Buffer<4>", {}).get("align") == 1,
                  "const generic instance layout")
            check("Wrapper<nested::NestedPoint>" in advanced_types,
                  "non-primitive generic argument extracted")
            check(advanced_types.get("Wrapper<nested::NestedPoint>", {}).get("size") == 8,
                  "non-primitive generic argument layout")
            check(bool(advanced.get("lifetimes")), "lifetime metadata extracted")
            check(bool(advanced.get("trait_objects")), "trait object metadata extracted")
            check(any(item.get("name") == "async_answer"
                      for item in advanced.get("async_functions", [])),
                  "async function metadata extracted")

            # Explicit instance arguments fail closed: unknown type names and
            # malformed const values do not create a fake instance.
            rejected_json = os.path.join(work, "instances_rejected.json")
            result = run([helper, "--crate-name", "rust_semantic", "--edition", "2021",
                          "--source", str(semantic_fixture), "--instantiate", "Wrapper=NoSuchType",
                          "--instantiate", "Buffer=notanumber",
                          "--output", rejected_json])
            if check(result.returncode == 0 and os.path.isfile(rejected_json),
                     "invalid instance arguments fail closed", output(result)[-300:]):
                rejected = json.load(open(rejected_json, encoding="utf-8"))
                rejected_names = {item.get("name") for item in rejected.get("types", [])}
                check(not any("NoSuchType" in name for name in rejected_names),
                      "unknown type argument is ignored")
                check(not any(name.startswith("Buffer<notanumber>") for name in rejected_names),
                      "invalid const argument is ignored")

            traits = advanced.get("traits", [])
            trait_decl = next((item for item in traits
                               if item.get("kind") == "trait" and item.get("name") == "Shape"), {})
            check(trait_decl.get("name") == "Shape", "trait declaration extracted")
            check({method.get("name") for method in trait_decl.get("methods", [])} == {"area", "perimeter"},
                  "trait methods extracted")
            circle_impl = next((item for item in traits if item.get("kind") == "impl"
                                and item.get("self_type", "").endswith("::Circle")), {})
            check(bool(circle_impl), "trait impl extracted")
            check(any(associated.get("name") == "Unit"
                      and associated.get("kind") == "type"
                      and associated.get("value") == "rust:f64"
                      for associated in circle_impl.get("associated_types", [])),
                  "trait impl associated type extracted")
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

        # Standard containers are recorded as semantics that need an explicit
        # contract. The helper does not assume their layout or ownership.
        container_fixture = source_root / "tests" / "fixtures" / "rust" / "containers" / "rust_containers.rs"
        container_json = os.path.join(work, "containers.json")
        result = run([helper, "--crate-name", "rust_containers", "--edition", "2021",
                      "--source", str(container_fixture), "--output", container_json])
        if check(result.returncode == 0 and os.path.isfile(container_json),
                 "helper extracted the container fixture", output(result)[-400:]):
            container_document = json.load(open(container_json, encoding="utf-8"))
            containers = {item.get("container"): item
                          for item in container_document.get("containers", [])}
            check(set(containers) == {"option", "box", "string", "vec"},
                  "standard containers are recorded")
            option = containers.get("option", {})
            check(option.get("context") == "parameter" and option.get("element") == "rust:i32",
                  "Option element type is recorded")
            check(containers.get("box", {}).get("element") == "rust:rust_containers::ContainerPayload",
                  "Box element type is recorded")
            check(containers.get("string", {}).get("element") is None,
                  "String has no element type")
            check(containers.get("vec", {}).get("element") == "rust:u8",
                  "Vec element type is recorded")
            check(any(item.get("owner", "").endswith("::container_option")
                      for item in container_document.get("containers", [])),
                  "container owner is recorded")

        # Trait objects stay semantic metadata; AMC must not map their fat
        # pointers as thin ABI pointers.
        trait_object_fixture = source_root / "tests" / "fixtures" / "rust" / "semantics" / "rust_trait_object.rs"
        trait_object_json = os.path.join(work, "trait_object.json")
        result = run([helper, "--crate-name", "rust_trait_object", "--edition", "2021",
                      "--source", str(trait_object_fixture), "--output", trait_object_json])
        if check(result.returncode == 0 and os.path.isfile(trait_object_json),
                 "helper extracted the trait-object fixture", output(result)[-400:]):
            trait_object_document = json.load(open(trait_object_json, encoding="utf-8"))
            objects = trait_object_document.get("trait_objects", [])
            check(any(item.get("owner", "").endswith("::trait_object_name")
                      and item.get("context") == "parameter"
                      and "dyn Named" in item.get("type", "")
                      for item in objects),
                  "trait object parameter is recorded")
            check(any(item.get("owner", "").endswith("::DynField")
                      and item.get("context") == "field"
                      for item in objects),
                  "trait object field is recorded")

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

        # Cargo workspace facts: members, default members, dependencies and the
        # cross-compilation target are recorded in the `cargo` extension.
        workspace_root = source_root / "tests" / "fixtures" / "rust" / "workspace"
        workspace_manifest = workspace_root / "app" / "Cargo.toml"
        workspace_source = workspace_root / "app" / "src" / "lib.rs"
        workspace_json = os.path.join(work, "cargo_workspace.json")
        host_triple = target.get("triple", "")
        result = run([helper, "--crate-name", "rust_workspace_app", "--edition", "2021",
                      "--source", str(workspace_source), "--manifest-path", str(workspace_manifest),
                      "--target", host_triple, "--features", "extra",
                      "--output", workspace_json])
        if check(result.returncode == 0 and os.path.isfile(workspace_json),
                 "helper reads cargo workspace metadata", output(result)[-400:]):
            workspace = json.load(open(workspace_json, encoding="utf-8"))
            workspace_cargo = workspace.get("cargo", {})
            check(Path(workspace_cargo.get("workspace_root", "")).name == "workspace",
                  "cargo workspace root is recorded")
            members = workspace_cargo.get("workspace_members", [])
            check(len(members) == 2 and any("rust_workspace_core" in item for item in members)
                  and any("rust_workspace_app" in item for item in members),
                  "cargo workspace members are recorded")
            default_members = workspace_cargo.get("default_members", [])
            check(len(default_members) == 1 and "rust_workspace_app" in default_members[0],
                  "cargo default members are recorded")
            dependencies = workspace_cargo.get("dependencies", [])
            core_dependency = next(
                (item for item in dependencies if item.get("name") == "rust_workspace_core"), {})
            check(core_dependency.get("kind") == "normal", "cargo dependency kind is recorded")
            check(Path(core_dependency.get("path", "")).name == "core",
                  "cargo path dependency is recorded")
            check(workspace.get("crate", {}).get("name") == "rust_workspace_app",
                  "cargo workspace package name")
            check(workspace_cargo.get("edition") == "2021", "cargo package edition is recorded")
            check(set(workspace.get("crate", {}).get("features", [])) >= {"default", "extra"},
                  "cargo workspace features are recorded")
            check(workspace.get("target", {}).get("triple") == host_triple,
                  "cargo target triple is forwarded to the helper")

        edition_source = Path(work) / "edition2024.rs"
        edition_source.write_text(
            "#[repr(C)]\npub struct Edition2024 { pub value: i32 }\n", encoding="utf-8")
        edition_json = os.path.join(work, "edition2024.json")
        result = run([helper, "--crate-name", "edition2024", "--edition", "2024",
                      "--source", str(edition_source), "--output", edition_json])
        if check(result.returncode == 0 and os.path.isfile(edition_json),
                 "helper compiles an explicit edition", output(result)[-400:]):
            edition_crate = json.load(open(edition_json, encoding="utf-8")).get("crate", {})
            check(edition_crate.get("edition") == "2024",
                  "helper reports the requested edition")

        feature_json = os.path.join(work, "features-without-manifest.json")
        result = run([helper, "--crate-name", "rust_semantic", "--edition", "2021",
                      "--source", str(semantic_fixture), "--features", "default,fast",
                      "--output", feature_json])
        if check(result.returncode == 0 and os.path.isfile(feature_json),
                 "helper records CLI features without a manifest", output(result)[-400:]):
            feature_crate = json.load(open(feature_json, encoding="utf-8")).get("crate", {})
            check(set(feature_crate.get("features", [])) == {"default", "fast"},
                  "CLI features survive a missing Cargo manifest")

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

            # A failing helper must surface the rustc diagnostics instead of
            # reporting only an exit status.
            broken_source = Path(work) / "broken_helper.rs"
            broken_source.write_text(
                "#[repr(C)]\npub struct BrokenHelper { pub value: i32 }\n\n"
                "pub fn broken_helper_value() -> i32 {\n"
                "    let value: i32 = \"not a number\";\n"
                "    value\n}\n", encoding="utf-8")
            broken_config = os.path.join(work, "broken_helper.abic.toml")
            with open(broken_config, "w", encoding="utf-8") as handle:
                handle.write(
                    "[package]\nname = \"broken_helper\"\nversion = \"0.1\"\n\n"
                    "[[import]]\nlanguage = \"rust\"\nfiles = [\"broken_helper.rs\"]\n"
                    "symbols = [\"BrokenHelper\"]\n\n"
                    "[rust]\nhelper = \"" + helper_toml + "\"\n\n"
                    "[[export]]\noutput = \"broken_helper.abix\"\n")
            result = run([str(amc), "build", "-c", broken_config, "-B", os.path.join(work, "broken_out")])
            check(result.returncode != 0 and "rust helper failed" in output(result)
                  and "E0308" in output(result),
                  "helper failure preserves rustc diagnostics", output(result)[-400:])

            mismatch_config = os.path.join(work, "mismatch.abic.toml")
            mismatch_text = fixture_config.read_text(encoding="utf-8").replace(
                'endianness = "little"', 'endianness = "big"', 1)
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
                "symbols = [\"Circle\", \"NestedPoint\", \"WrapperI32\", \"Wrapper<u64>\", \"Buffer<4>\", \"nested_sum\", \"rust_status\"]\n\n"
                "[[instantiate]]\ntemplate = \"Wrapper\"\nargs = [\"u64\"]\nas = \"WrapperU64\"\n\n"
                "[[instantiate]]\ntemplate = \"Buffer\"\nargs = [\"4\"]\nas = \"Buffer4\"\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\nfeatures = [\"default\", \"fast\"]\n\n"
                "[[error]]\nfunction = \"rust_status\"\nstatus = \"return\"\nsuccess = 0\nvalue = -1\n"
                "domain = 0x1003000000000001\n\n"
                "[[export]]\noutput = \"rust_semantic.abix\"\n",
                encoding="utf-8")
            import_out = import_dir / "out"
            result = run([str(amc), "build", "-c", str(import_config), "-B", str(import_out)])
            import_artifact = import_out / "rust_semantic.abix"
            if check(result.returncode == 0 and import_artifact.is_file(),
                     "AMC imports helper semantic entities", output(result)[-400:]):
                check("layout differs from the source parser" not in output(result),
                      "AMC cross-validates helper and parser layouts")
                check(abix_error_codes(import_artifact) == {1, 2},
                      "explicit [[error]] reuses helper error variants")
                semantic_path = Path(str(import_artifact) + ".semantic")
                check(semantic_path.is_file(), "AMC writes the imported semantic sidecar")
                if semantic_path.is_file():
                    semantic = json.loads(semantic_path.read_text(encoding="utf-8"))
                    names = {item.get("name") for item in semantic.get("types", [])}
                    check({"NestedPoint", "WrapperI32", "Wrapper<i32>", "Wrapper<u64>", "Buffer<4>"} <= names,
                          "helper types reached Semantic IR")
                    check(bool(semantic.get("functions")), "helper functions reached Semantic IR")
                    rust = semantic.get("rust", {})
                    check(bool(rust.get("generics")), "Rust generics reached Semantic IR")
                    check(bool(rust.get("traits")), "Rust traits reached Semantic IR")
                    circle_impl = next((item for item in rust.get("traits", [])
                                        if item.get("kind") == "impl"
                                        and item.get("self_type", "").endswith("::Circle")), {})
                    check(any(associated.get("name") == "Unit"
                              and associated.get("value") == "rust:f64"
                              for associated in circle_impl.get("associated_types", [])),
                          "trait impl associated type reached Semantic IR")
                    check(bool(rust.get("errors")), "Rust errors reached Semantic IR")
                    check(bool(rust.get("lifetimes")), "Rust lifetimes reached Semantic IR")
                    check(bool(rust.get("trait_objects")), "Rust trait objects reached Semantic IR")
                    check(any(item.get("name") == "async_answer"
                              for item in rust.get("async_functions", [])),
                          "Rust async functions reached Semantic IR")
                query = run([str(amc), "query", str(import_artifact), "--type", "WrapperI32",
                             "--format", "json", "--layout"])
                if check(query.returncode == 0, "AMC queries the imported alias", output(query)[-300:]):
                    match = json.loads(query.stdout).get("matches", [{}])[0]
                    check(match.get("size") == 4 and match.get("align") == 4,
                          "imported alias keeps the helper layout")
                query = run([str(amc), "query", str(import_artifact), "--type", "Wrapper<u64>",
                             "--format", "json", "--layout"])
                if check(query.returncode == 0, "AMC queries an explicit generic instance",
                         output(query)[-300:]):
                    match = json.loads(query.stdout).get("matches", [{}])[0]
                    check(match.get("size") == 8 and match.get("align") == 8,
                          "explicit generic instance keeps the helper layout")
                query = run([str(amc), "query", str(import_artifact), "--type", "Buffer<4>",
                             "--format", "json", "--layout"])
                if check(query.returncode == 0, "AMC queries a const generic instance",
                         output(query)[-300:]):
                    match = json.loads(query.stdout).get("matches", [{}])[0]
                    check(match.get("size") == 4 and match.get("align") == 1,
                          "const generic instance keeps the helper layout")
                query = run([str(amc), "query", str(import_artifact), "--type", "NestedPoint",
                             "--format", "json", "--layout"])
                if check(query.returncode == 0, "AMC queries the imported nested record", output(query)[-300:]):
                    match = json.loads(query.stdout).get("matches", [{}])[0]
                    check(match.get("size") == 8 and match.get("align") == 4,
                          "imported nested record keeps the helper layout")

            # [rust] manifest / target / features: AMC forwards the Cargo
            # workspace manifest and target triple to the helper and stores the
            # workspace facts in the semantic sidecar.
            workspace_dir = Path(work) / "workspace_import"
            workspace_dir.mkdir()
            shutil.copyfile(workspace_source, workspace_dir / "lib.rs")
            workspace_config = workspace_dir / "workspace.abic.toml"
            workspace_config.write_text(
                "[package]\nname = \"rust_workspace_app\"\nversion = \"0.2.0\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"lib.rs\"]\n"
                "symbols = [\"WorkspaceApp\", \"workspace_app_value\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\n"
                "manifest = \"" + str(workspace_manifest).replace("\\", "/") + "\"\n"
                "target = \"" + host_triple + "\"\nfeatures = [\"extra\"]\n\n"
                "[[export]]\noutput = \"workspace.abix\"\n",
                encoding="utf-8")
            workspace_out = workspace_dir / "out"
            result = run([str(amc), "build", "-c", str(workspace_config), "-B", str(workspace_out)])
            workspace_artifact = workspace_out / "workspace.abix"
            if check(result.returncode == 0 and workspace_artifact.is_file(),
                     "AMC forwards [rust] manifest/target/features", output(result)[-500:]):
                workspace_semantic = Path(str(workspace_artifact) + ".semantic")
                if check(workspace_semantic.is_file(), "workspace semantic sidecar written"):
                    semantic = json.loads(workspace_semantic.read_text(encoding="utf-8"))
                    cargo = semantic.get("rust", {}).get("cargo", {})
                    check(Path(cargo.get("workspace_root", "")).name == "workspace",
                          "AMC stores the cargo workspace root")
                    check(len(cargo.get("workspace_members", [])) == 2,
                          "AMC stores cargo workspace members")
                    check(any(item.get("name") == "rust_workspace_core"
                              for item in cargo.get("dependencies", [])),
                          "AMC stores cargo dependencies")
                    check(cargo.get("target_directory") != "", "AMC stores the cargo target directory")

            # [rust] edition selects the rustc edition and is validated early.
            edition_dir = Path(work) / "edition_import"
            edition_dir.mkdir()
            shutil.copyfile(edition_source, edition_dir / "edition2024.rs")
            edition_config = edition_dir / "edition.abic.toml"
            edition_config.write_text(
                "[package]\nname = \"edition2024\"\nversion = \"0.1\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"edition2024.rs\"]\n"
                "symbols = [\"Edition2024\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\nedition = \"2024\"\n\n"
                "[[export]]\noutput = \"edition2024.abix\"\n",
                encoding="utf-8")
            result = run([str(amc), "build", "-c", str(edition_config),
                          "-B", str(edition_dir / "out")])
            check(result.returncode == 0
                  and (edition_dir / "out" / "edition2024.abix").is_file(),
                  "AMC forwards [rust] edition", output(result)[-500:])

            bad_edition = edition_dir / "bad_edition.abic.toml"
            bad_edition.write_text(
                edition_config.read_text(encoding="utf-8").replace(
                    'edition = "2024"', 'edition = "bogus"', 1),
                encoding="utf-8")
            result = run([str(amc), "build", "-c", str(bad_edition),
                          "-B", str(edition_dir / "bad_out")])
            bad_output = output(result)
            check(result.returncode != 0 and "[rust] edition must be" in bad_output,
                  "invalid [rust] edition fails closed", bad_output[-400:])

            # Standard containers are fail-closed when selected for ABI, but
            # their facts still reach the semantic sidecar.
            container_dir = Path(work) / "container_import"
            container_dir.mkdir()
            shutil.copyfile(container_fixture, container_dir / "rust_containers.rs")
            container_config = container_dir / "container.abic.toml"
            container_config.write_text(
                "[package]\nname = \"rust_containers\"\nversion = \"0.1\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"rust_containers.rs\"]\n"
                "symbols = [\"ContainerPayload\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\n\n"
                "[[export]]\noutput = \"rust_containers.abix\"\n",
                encoding="utf-8")
            container_out = container_dir / "out"
            result = run([str(amc), "build", "-c", str(container_config), "-B", str(container_out)])
            container_artifact = container_out / "rust_containers.abix"
            if check(result.returncode == 0 and container_artifact.is_file(),
                     "container facts do not block unselected helpers", output(result)[-500:]):
                container_semantic = Path(str(container_artifact) + ".semantic")
                if check(container_semantic.is_file(), "container semantic sidecar written"):
                    semantic = json.loads(container_semantic.read_text(encoding="utf-8"))
                    recorded = {item.get("container")
                                for item in semantic.get("rust", {}).get("containers", [])}
                    check(recorded == {"option", "box", "string", "vec"},
                          "container facts reached Semantic IR")

            selected_config = container_dir / "container_selected.abic.toml"
            selected_config.write_text(
                "[package]\nname = \"rust_containers\"\nversion = \"0.1\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"rust_containers.rs\"]\n"
                "symbols = [\"ContainerPayload\", \"container_option\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\n\n"
                "[[export]]\noutput = \"rust_containers.abix\"\n",
                encoding="utf-8")
            result = run([str(amc), "build", "-c", str(selected_config),
                          "-B", str(container_dir / "selected_out")])
            selected_output = output(result)
            check(result.returncode != 0 and "Option<T>" in selected_output
                  and "explicit [[contract]]" in selected_output,
                  "selected container requires an explicit contract", selected_output[-500:])

            # Trait objects must be rejected instead of being lowered as thin
            # pointers, both in function signatures and record fields.
            trait_dir = Path(work) / "trait_object_import"
            trait_dir.mkdir()
            shutil.copyfile(trait_object_fixture, trait_dir / "rust_trait_object.rs")
            trait_config = trait_dir / "trait.abic.toml"
            trait_config.write_text(
                "[package]\nname = \"rust_trait_object\"\nversion = \"0.1\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"rust_trait_object.rs\"]\n"
                "symbols = [\"DynPayload\", \"trait_object_name\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\n\n"
                "[[export]]\noutput = \"rust_trait_object.abix\"\n",
                encoding="utf-8")
            result = run([str(amc), "build", "-c", str(trait_config), "-B", str(trait_dir / "out")])
            trait_output = output(result)
            check(result.returncode != 0 and "trait object (dyn Trait)" in trait_output,
                  "trait-object parameter is rejected", trait_output[-500:])

            field_config = trait_dir / "trait_field.abic.toml"
            field_config.write_text(
                "[package]\nname = \"rust_trait_object\"\nversion = \"0.1\"\n\n"
                "[[import]]\nlanguage = \"rust\"\nfiles = [\"rust_trait_object.rs\"]\n"
                "symbols = [\"DynField\"]\n\n"
                "[rust]\nhelper = \"" + helper_toml + "\"\n\n"
                "[[export]]\noutput = \"rust_trait_field.abix\"\n",
                encoding="utf-8")
            result = run([str(amc), "build", "-c", str(field_config), "-B", str(trait_dir / "field_out")])
            field_output = output(result)
            check(result.returncode != 0 and "field 'DynField::name'" in field_output,
                  "trait-object field is rejected", field_output[-500:])

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
