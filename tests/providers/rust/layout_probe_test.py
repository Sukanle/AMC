#!/usr/bin/env python3
"""Compare AMC's Rust layout fallback with rustc for the FFI fixture.

This is the first v0.3.1 layout probe: generated Rust code uses
`core::mem::{size_of, align_of, offset_of!}` and the result is compared with
`amc query --format json --layout`. It deliberately targets the Host so it can
run as a normal test; cross-target reflection will use a compiled section later.

Usage:
    layout_probe_test.py <amc_bin_dir> [source_root]
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import contextlib


@contextlib.contextmanager
def preserved_directory(prefix: str):
    """Keep the work directory when KEEP_ARTIFACTS=1 or --keep-artifacts."""
    work = tempfile.mkdtemp(prefix=prefix)
    keep = os.environ.get("KEEP_ARTIFACTS", "") == "1" or "--keep-artifacts" in sys.argv[1:]
    try:
        yield work
    finally:
        if keep:
            print("  kept artifacts in " + work)
        else:
            shutil.rmtree(work, ignore_errors=True)


# Types/fields in tests/fixtures/rust_demo.rs that participate in the probe.
CASES = {
    "RustPoint": ["x", "y"],
    "RustRect": ["origin", "width", "height"],
    "RustColor": [],
    "RustBuffer": ["data", "len"],
    "RustIntPtrs": ["signed", "unsigned"],
    "RustArray": ["tag", "values"],
    "RustTransparent": ["code"],
    "RustHandle": [],
    "RustSmallColor": [],
    "RustAligned": ["value"],
}

PACKED_CASES = {
    "RustPacked": ["kind", "value"],
}

ADVANCED_CASES = {
    "RustPacked2": ["kind", "value"],
    "RustUnion": [],
}


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def fail(message: str) -> None:
    print(f"  [FAIL] {message}")


def info(message: str) -> None:
    print(f"  [INFO] {message}")


def pass_(message: str) -> None:
    print(f"  [PASS] {message}")


def run(args, **kwargs):
    return subprocess.run(args, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", **kwargs)


def build_artifact(amc: Path, config: Path, build_dir: Path,
                   artifact_name: str = "rust_demo.abix") -> Path:
    result = run([str(amc), "build", "-c", str(config), "-B", str(build_dir)])
    artifact = build_dir / artifact_name
    if result.returncode != 0 or not artifact.is_file():
        raise RuntimeError(f"amc build failed: {result.stderr.strip()}")
    return artifact


def query_layout(amc: Path, artifact: Path, type_name: str) -> dict:
    result = run([str(amc), "query", str(artifact), "--type", type_name,
                  "--format", "json", "--layout"])
    if result.returncode != 0:
        raise RuntimeError(f"amc query {type_name} failed: {result.stderr.strip()}")
    document = json.loads(result.stdout)
    matches = document.get("matches", [])
    if len(matches) != 1:
        raise RuntimeError(f"amc query {type_name}: expected one match, got {len(matches)}")
    return matches[0]


def write_probe(source: Path, probe: Path, cases=CASES) -> None:
    lines = [
        "use std::mem::{align_of, offset_of, size_of};",
        f'#[path = r"{source}"]',
        "mod fixture;",
        "fn main() {",
    ]
    for type_name, fields in cases.items():
        lines.append(
            f'    println!("{type_name}.size {{}}", size_of::<fixture::{type_name}>());'
        )
        lines.append(
            f'    println!("{type_name}.align {{}}", align_of::<fixture::{type_name}>());'
        )
        for field in fields:
            lines.append(
                f'    println!("{type_name}.{field}.offset {{}}", '
                f"offset_of!(fixture::{type_name}, {field}));"
            )
    lines.append("}")
    probe.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_probe(output: str) -> dict:
    parsed: dict[str, dict[str, int]] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        name, value = parts
        try:
            number = int(value)
        except ValueError:
            continue
        if name.endswith(".size"):
            parsed.setdefault(name[:-5], {})["size"] = number
        elif name.endswith(".align"):
            parsed.setdefault(name[:-6], {})["align"] = number
        elif name.endswith(".offset"):
            owner, field = name[:-7].rsplit(".", 1)
            parsed.setdefault(owner, {})[f"offset:{field}"] = number
    return parsed


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bin_dir = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path(__file__).resolve().parents[3]
    amc = bin_dir / exe_name("amc")
    rustc = shutil.which("rustc")
    if rustc is None:
        info("rustc not found: skipping Rust layout probe")
        return 0
    if not amc.is_file():
        print(f"amc not found: {amc}", file=sys.stderr)
        return 1

    fixture_dir = source_root / "tests" / "fixtures"
    config = fixture_dir / "rust_demo.abic.toml"
    source = fixture_dir / "rust_demo.rs"

    # R0 toolchain facts: the configured target must agree with rustc's Host
    # facts for the Host layout probe. Cross-target probe is a later step.
    version = run([rustc, "-vV"])
    if version.returncode != 0:
        print(f"  [FAIL] rustc -vV failed: {version.stderr.strip()}", file=sys.stderr)
        return 1
    host = next((line.split(":", 1)[1].strip() for line in version.stdout.splitlines()
                 if line.startswith("host:")), "<unknown>")
    release = next((line.split(":", 1)[1].strip() for line in version.stdout.splitlines()
                    if line.startswith("release:")), "<unknown>")
    info(f"rustc {release} host={host}")
    cfg = run([rustc, "--print", "cfg"])
    pointer_width = next((m.group(1) for m in re.finditer(r'target_pointer_width="(\d+)"', cfg.stdout)), "")
    target_endian = next((m.group(1) for m in re.finditer(r'target_endian="(\w+)"', cfg.stdout)), "")
    config_text = config.read_text(encoding="utf-8")
    configured_pointer = next((m.group(1) for m in re.finditer(r"pointer_bits\s*=\s*(\d+)", config_text)), "")
    configured_endian = next((m.group(1) for m in re.finditer(r'endianness\s*=\s*"([^"]+)"', config_text)), "")
    if configured_pointer != pointer_width:
        fail(f"configured pointer_bits={configured_pointer} but rustc Host pointer_width={pointer_width}")
        return 1
    if configured_endian != target_endian:
        fail(f"configured endianness={configured_endian} but rustc Host endian={target_endian}")
        return 1
    pass_(f"configured target matches rustc Host facts ({pointer_width}-bit, {target_endian})")

    with preserved_directory("amc-rust-layout-") as temporary:
        work = Path(temporary)
        try:
            artifact = build_artifact(amc, config, work / "build")
            probe_source = work / "probe.rs"
            probe_exe = work / exe_name("probe")
            write_probe(source, probe_source)
            compile_result = run([rustc, "--edition", "2021", str(probe_source), "-o", str(probe_exe)])
            if compile_result.returncode != 0:
                raise RuntimeError(f"rustc probe failed: {compile_result.stderr.strip()}")
            probe_result = run([str(probe_exe)])
            if probe_result.returncode != 0:
                raise RuntimeError(f"probe execution failed: {probe_result.stderr.strip()}")
            expected = parse_probe(probe_result.stdout)
            if not expected:
                raise RuntimeError("probe produced no layout facts")
        except RuntimeError as error:
            print(f"  [FAIL] {error}", file=sys.stderr)
            return 1

        failures = 0
        for type_name, fields in CASES.items():
            layout = query_layout(amc, artifact, type_name)
            rust = expected.get(type_name, {})
            if rust.get("size") != layout.get("size"):
                fail(f"{type_name} size: amc={layout.get('size')} rustc={rust.get('size')}")
                failures += 1
            else:
                pass_(f"{type_name} size matches rustc ({layout.get('size')})")
            if rust.get("align") != layout.get("align"):
                fail(f"{type_name} align: amc={layout.get('align')} rustc={rust.get('align')}")
                failures += 1
            else:
                pass_(f"{type_name} align matches rustc ({layout.get('align')})")
            amc_fields = {field["name"]: field["offset"] for field in layout.get("fields", [])}
            for field in fields:
                if rust.get(f"offset:{field}") != amc_fields.get(field):
                    fail(f"{type_name}.{field} offset: amc={amc_fields.get(field)} rustc={rust.get(f'offset:{field}')}")
                    failures += 1
                else:
                    pass_(f"{type_name}.{field} offset matches rustc ({amc_fields[field]})")

        if failures:
            print(f"  [FAIL] {failures} Rust layout mismatch(es)")
            return 1
        print("  [PASS] AMC layout matches rustc probe")

        # Target-aware fallback: a 32-bit target must change pointer/usize/isize
        # and the default c_long width without relying on the Host.
        try:
            config_text = config.read_text(encoding="utf-8")
            config_text = config_text.replace("pointer_bits = 64", "pointer_bits = 32")
            config32 = work / "rust_demo32.abic.toml"
            config32.write_text(config_text, encoding="utf-8")
            shutil.copyfile(source, work / "rust_demo.rs")
            artifact32 = build_artifact(amc, config32, work / "build32")
            buffer32 = query_layout(amc, artifact32, "RustBuffer")
            fields32 = {field["name"]: field["offset"] for field in buffer32.get("fields", [])}
            intptrs32 = query_layout(amc, artifact32, "RustIntPtrs")
            int_fields32 = {field["name"]: field["offset"] for field in intptrs32.get("fields", [])}
            checks = [
                ("32-bit RustBuffer size", buffer32.get("size"), 8),
                ("32-bit RustBuffer.len offset", fields32.get("len"), 4),
                ("32-bit RustIntPtrs size", intptrs32.get("size"), 8),
                ("32-bit RustIntPtrs.signed offset", int_fields32.get("signed"), 0),
                ("32-bit RustIntPtrs.unsigned offset", int_fields32.get("unsigned"), 4),
            ]
            for description, actual, expected in checks:
                if actual != expected:
                    fail(f"{description}: amc={actual} expected={expected}")
                    failures += 1
                else:
                    pass_(f"{description} = {expected}")
        except RuntimeError as error:
            print(f"  [FAIL] 32-bit target check: {error}", file=sys.stderr)
            return 1

        if failures:
            print(f"  [FAIL] {failures} target-aware layout mismatch(es)")
            return 1
        print("  [PASS] target-aware pointer widths match the configured target")

        # Packed structs are layout-only for now: the backends do not yet emit
        # repr(C, packed), so this fixture is not sent through amc generate.
        try:
            packed_config = fixture_dir / "rust_packed.abic.toml"
            packed_source = fixture_dir / "rust_packed.rs"
            packed_artifact = build_artifact(amc, packed_config, work / "packed-build", "rust_packed.abix")
            packed_probe_source = work / "probe_packed.rs"
            packed_probe_exe = work / exe_name("probe_packed")
            write_probe(packed_source, packed_probe_source, PACKED_CASES)
            packed_compile = run([rustc, "--edition", "2021", str(packed_probe_source),
                                  "-o", str(packed_probe_exe)])
            if packed_compile.returncode != 0:
                raise RuntimeError(f"rustc packed probe failed: {packed_compile.stderr.strip()}")
            packed_run = run([str(packed_probe_exe)])
            if packed_run.returncode != 0:
                raise RuntimeError(f"packed probe execution failed: {packed_run.stderr.strip()}")
            packed_expected = parse_probe(packed_run.stdout)
            packed_layout = query_layout(amc, packed_artifact, "RustPacked")
            packed_rust = packed_expected.get("RustPacked", {})
            packed_fields = {field["name"]: field["offset"] for field in packed_layout.get("fields", [])}
            checks = [
                ("RustPacked size", packed_layout.get("size"), packed_rust.get("size")),
                ("RustPacked align", packed_layout.get("align"), packed_rust.get("align")),
                ("RustPacked.kind offset", packed_fields.get("kind"), packed_rust.get("offset:kind")),
                ("RustPacked.value offset", packed_fields.get("value"), packed_rust.get("offset:value")),
            ]
            for description, actual, expected_value in checks:
                if actual != expected_value:
                    fail(f"{description}: amc={actual} rustc={expected_value}")
                    failures += 1
                else:
                    pass_(f"{description} matches rustc ({actual})")
        except RuntimeError as error:
            print(f"  [FAIL] packed layout check: {error}", file=sys.stderr)
            return 1

        if failures:
            print(f"  [FAIL] {failures} packed layout mismatch(es)")
            return 1
        print("  [PASS] packed layout matches rustc probe")

        # packed(N) and unions are layout-only too: the backends do not emit
        # them yet, so they stay out of the projection conformance path.
        try:
            advanced_config = fixture_dir / "rust_advanced.abic.toml"
            advanced_source = fixture_dir / "rust_advanced.rs"
            advanced_artifact = build_artifact(amc, advanced_config, work / "advanced-build",
                                               "rust_advanced.abix")
            advanced_probe_source = work / "probe_advanced.rs"
            advanced_probe_exe = work / exe_name("probe_advanced")
            write_probe(advanced_source, advanced_probe_source, ADVANCED_CASES)
            advanced_compile = run([rustc, "--edition", "2021", str(advanced_probe_source),
                                    "-o", str(advanced_probe_exe)])
            if advanced_compile.returncode != 0:
                raise RuntimeError(f"rustc advanced probe failed: {advanced_compile.stderr.strip()}")
            advanced_run = run([str(advanced_probe_exe)])
            if advanced_run.returncode != 0:
                raise RuntimeError(f"advanced probe execution failed: {advanced_run.stderr.strip()}")
            advanced_expected = parse_probe(advanced_run.stdout)
            for type_name, fields in ADVANCED_CASES.items():
                layout = query_layout(amc, advanced_artifact, type_name)
                rust = advanced_expected.get(type_name, {})
                checks = [
                    (f"{type_name} size", layout.get("size"), rust.get("size")),
                    (f"{type_name} align", layout.get("align"), rust.get("align")),
                ]
                for field in fields:
                    amc_fields = {item["name"]: item["offset"] for item in layout.get("fields", [])}
                    checks.append((f"{type_name}.{field} offset", amc_fields.get(field),
                                   rust.get(f"offset:{field}")))
                for description, actual, expected_value in checks:
                    if actual != expected_value:
                        fail(f"{description}: amc={actual} rustc={expected_value}")
                        failures += 1
                    else:
                        pass_(f"{description} matches rustc ({actual})")
            union = query_layout(amc, advanced_artifact, "RustUnion")
            union_fields = {item["name"]: item["offset"] for item in union.get("fields", [])}
            for field in ("word", "wide"):
                if union_fields.get(field) != 0:
                    fail(f"RustUnion.{field} offset: amc={union_fields.get(field)} expected=0")
                    failures += 1
                else:
                    pass_(f"RustUnion.{field} offset = 0")
        except RuntimeError as error:
            print(f"  [FAIL] advanced repr layout check: {error}", file=sys.stderr)
            return 1

        if failures:
            print(f"  [FAIL] {failures} advanced repr mismatch(es)")
            return 1
        print("  [PASS] advanced repr layout matches rustc probe")
        return 0


if __name__ == "__main__":
    sys.exit(main())
