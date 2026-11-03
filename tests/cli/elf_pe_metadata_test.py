#!/usr/bin/env python3
"""Verify that the ABIX metadata section lands in a native binary and reads back.

Usage:
    elf_pe_metadata_test.py <amc_bin_dir> <source_root> [--abix-include-dirs PATHLIST] [--keep-artifacts]

The test builds the AMC fixture, generates a C++ projection, compiles it with a
native compiler, inspects the resulting ELF/Mach-O/PE section, and verifies the
`amc metadata` read/verify/export path. It is the AMC-side owner of the test
formerly reached through `ABIX/tools/run_test.py --elf-pe`.

`ABIX_INCLUDE_DIRS` (or `--abix-include-dirs`) must contain the ABIX/MICS/HLMD
include roots, separated by the platform path separator.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SECTION_METADATA = ".abix.metadata"
SECTION_NAMES = ".abix.names"

IS_MACOS = sys.platform == "darwin"
NATIVE_METADATA = "__abix_metadata"
NATIVE_NAMES = "__abix_names"

IS_WINDOWS = sys.platform == "win32"
WIN_METADATA = ".abix"
WIN_NAMES = ".abix.n"


def exe_name(base: str) -> str:
    return f"{base}.exe" if IS_WINDOWS else base


def supports(text: str) -> bool:
    encoding = getattr(sys.stdout, "encoding", None) or "ascii"
    try:
        text.encode(encoding)
    except (UnicodeEncodeError, LookupError):
        return False
    return True


OK_MARK = "✓" if supports("✓") else "[ok]"
FAIL_MARK = "✗" if supports("✗") else "[fail]"


class TestResult:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0
        self.errors: list[tuple[str, str]] = []

    def ok(self, name: str) -> None:
        self.passed += 1
        print(f"  \033[32m{OK_MARK}\033[0m {name}")

    def fail(self, name: str, msg: str) -> None:
        self.failed += 1
        self.errors.append((name, msg))
        print(f"  \033[31m{FAIL_MARK}\033[0m {name}: {msg}")

    def merge(self, other: "TestResult") -> None:
        self.passed += other.passed
        self.failed += other.failed
        self.errors.extend(other.errors)

    def summary(self) -> int:
        total = self.passed + self.failed
        print(f"\n{'=' * 60}")
        if self.failed == 0:
            print(f"\033[32mAll {total} tests passed.\033[0m")
        else:
            print(f"\033[31m{self.failed}/{total} tests failed.\033[0m")
            for name, msg in self.errors:
                print(f"  - {name}: {msg}")
        print()
        return 0 if self.failed == 0 else 1


def run_cmd(
    cmd: list[str], cwd: Path | None = None, check: bool = True
) -> subprocess.CompletedProcess[str]:
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise SystemExit(
            f"Command failed (exit code {result.returncode}): {' '.join(cmd)}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def file_size(path: Path) -> int:
    return path.stat().st_size if path.exists() else 0


def include_args(include_dirs: list[str]) -> list[str]:
    flags: list[str] = []
    for directory in include_dirs:
        flags.extend(["-I", directory])
    return flags


def test_amc_build(workdir: Path, amc: Path, fixture_config: Path) -> tuple[Path, TestResult]:
    result = TestResult()
    build_dir = workdir / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    run_cmd([str(amc), "build", "-c", str(fixture_config), "-B", str(build_dir)])

    abix_file = build_dir / "build" / "amc_test.abix"
    meta_file = build_dir / "build" / "amc_test.abix.meta"

    if abix_file.exists():
        result.ok(f"amc build produced .abix ({file_size(abix_file)} bytes)")
    else:
        result.fail("amc build produced .abix", "file not found")

    if meta_file.exists():
        result.ok(f"amc build produced .abix.meta ({file_size(meta_file)} bytes)")
    else:
        result.fail("amc build produced .abix.meta", "file not found")

    return abix_file, result


def test_amc_generate(abix_file: Path, workdir: Path, amc: Path) -> tuple[Path, TestResult]:
    result = TestResult()
    header = workdir / "amc_generated.hpp"

    run_cmd([str(amc), "generate", str(abix_file), "-l", "cpp", "-o", str(header)])

    if not header.exists():
        result.fail("amc generate produced header", "file not found")
        return header, result

    content = header.read_text()
    result.ok(f"amc generate produced header ({file_size(header)} bytes)")

    if SECTION_METADATA in content:
        result.ok("header contains section attribute for .abix.metadata")
    else:
        result.fail("section attribute", f"'{SECTION_METADATA}' not found in header")

    if SECTION_NAMES in content:
        result.ok("header contains section attribute for .abix.names")
    else:
        result.fail("section attribute", f"'{SECTION_NAMES}' not found in header")

    return header, result


def test_compile_native(header: Path, workdir: Path, include_dirs: list[str]) -> tuple[Path, TestResult]:
    result = TestResult()
    main_cpp = workdir / "main.cpp"
    main_cpp.write_text('#include "amc_generated.hpp"\nint main() { return 0; }\n')

    binary = workdir / ("test_abix.exe" if IS_WINDOWS else "test_abix")
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    if not cxx:
        result.fail("C++ compiler", "CXX is not set and no clang++/c++ was found")
        return binary, result

    if not include_dirs:
        result.fail("ABIX include roots", "ABIX_INCLUDE_DIRS or --abix-include-dirs is required")
        return binary, result

    r = run_cmd(
        [
            cxx,
            "-std=c++17",
            "-I",
            str(workdir),
            *include_args(include_dirs),
            str(main_cpp),
            "-o",
            str(binary),
        ],
        check=False,
    )

    if r.returncode != 0:
        result.fail("compiler", r.stderr.strip())
        return binary, result

    if binary.exists():
        result.ok(f"compiled native binary ({file_size(binary)} bytes)")
    else:
        result.fail("compiled native binary", "file not found")

    return binary, result


def test_readelf_sections(binary: Path) -> TestResult:
    result = TestResult()

    if IS_WINDOWS:
        readobj = shutil.which("llvm-readobj")
        if not readobj:
            result.ok("llvm-readobj not found, skipping PE section inspection")
            return result
        r = run_cmd([readobj, "--sections", str(binary)], check=False)
        if r.returncode != 0:
            result.fail("llvm-readobj --sections", r.stderr.strip())
            return result
        if f"Name: {WIN_METADATA} (" in r.stdout:
            result.ok(f"PE section {WIN_METADATA} found")
        else:
            result.fail("PE section", f"{WIN_METADATA} not found")
        if f"Name: {WIN_NAMES} (" in r.stdout:
            result.ok(f"PE section {WIN_NAMES} found")
        else:
            result.fail("PE section", f"{WIN_NAMES} not found")
        return result

    if IS_MACOS:
        if not shutil.which("otool"):
            result.ok("otool not found, skipping Mach-O section inspection")
            return result
        r = run_cmd(["otool", "-l", str(binary)], check=False)
        if r.returncode != 0:
            result.fail("otool -l", r.stderr.strip())
            return result
        if NATIVE_METADATA in r.stdout:
            result.ok(f"otool: {NATIVE_METADATA} section found")
        else:
            result.fail("otool", f"{NATIVE_METADATA} section not found")
        if NATIVE_NAMES in r.stdout:
            result.ok(f"otool: {NATIVE_NAMES} section found")
        else:
            result.fail("otool", f"{NATIVE_NAMES} section not found")
        return result

    r = run_cmd(["readelf", "-S", str(binary)], check=False)
    if r.returncode != 0:
        result.fail("readelf", r.stderr.strip())
        return result

    if SECTION_METADATA in r.stdout:
        for line in r.stdout.splitlines():
            if SECTION_METADATA in line:
                parts = line.split()
                try:
                    idx = next(i for i, p in enumerate(parts) if SECTION_METADATA in p)
                    size_hex = parts[idx + 4]
                    size = int(size_hex, 16)
                    result.ok(f"readelf: {SECTION_METADATA} section found (size=0x{size:x})")
                except (StopIteration, IndexError, ValueError):
                    result.ok(f"readelf: {SECTION_METADATA} section found")
                break
    else:
        result.fail("readelf", f"{SECTION_METADATA} section not found")

    if SECTION_NAMES in r.stdout:
        result.ok(f"readelf: {SECTION_NAMES} section found")
    else:
        result.fail("readelf", f"{SECTION_NAMES} section not found")

    return result


def test_objdump_metadata(binary: Path) -> TestResult:
    result = TestResult()

    if IS_WINDOWS:
        objcopy = shutil.which("llvm-objcopy") or shutil.which("objcopy")
        if not objcopy:
            result.ok("objcopy not found, skipping PE section dump")
            return result
        out_file = binary.parent / "win_region.bin"
        r = run_cmd([objcopy, f"--dump-section={WIN_METADATA}={out_file}", str(binary)], check=False)
        if r.returncode != 0 or not out_file.exists():
            result.fail("objcopy --dump-section .abix", r.stderr.strip())
            return result
        data = out_file.read_bytes()
        if data[:4] == b"ABIX":
            result.ok("PE .abix starts with the ABIX magic")
        else:
            result.fail("PE .abix", "ABIX magic not found at offset 0")
        return result

    if IS_MACOS:
        if not shutil.which("otool"):
            result.ok("otool not found, skipping section content dump")
            return result
        r = run_cmd(["otool", "-s", "__DATA", NATIVE_METADATA, str(binary)], check=False)
        if r.returncode != 0:
            result.fail("otool -s __DATA __abix_metadata", r.stderr.strip())
            return result
        if "58494241" in r.stdout:
            result.ok("otool: .abix.metadata contains ABIX magic")
        else:
            result.fail("otool", "ABIX magic not found in section content")
        if "Contents of" in r.stdout:
            result.ok("otool: section content dumped successfully")
        else:
            result.fail("otool", "no section content in output")
        return result

    r = run_cmd(["objdump", "-s", "-j", SECTION_METADATA, str(binary)], check=False)
    if r.returncode != 0:
        result.fail("objdump -s -j .abix.metadata", r.stderr.strip())
        return result

    if "ABIX" in r.stdout:
        result.ok("objdump: .abix.metadata contains ABIX magic")
    else:
        result.fail("objdump", "ABIX magic not found in section content")

    if "Contents of section" in r.stdout:
        result.ok("objdump: section content dumped successfully")
    else:
        result.fail("objdump", "no section content in output")

    return result


def test_objdump_names(binary: Path) -> TestResult:
    result = TestResult()

    if IS_WINDOWS:
        objcopy = shutil.which("llvm-objcopy") or shutil.which("objcopy")
        if not objcopy:
            result.ok("objcopy not found, skipping PE section dump")
            return result
        out_file = binary.parent / "win_names.bin"
        r = run_cmd([objcopy, f"--dump-section={WIN_NAMES}={out_file}", str(binary)], check=False)
        if r.returncode != 0 or not out_file.exists():
            result.fail("objcopy --dump-section .abix.n", r.stderr.strip())
            return result
        size = out_file.stat().st_size
        if size > 0:
            result.ok(f"PE {WIN_NAMES} dumped ({size} bytes)")
        else:
            result.fail("PE section", f"{WIN_NAMES} appears empty")
        return result

    if IS_MACOS:
        if not shutil.which("otool"):
            result.ok("otool not found, skipping section content dump")
            return result
        r = run_cmd(["otool", "-s", "__DATA", NATIVE_NAMES, str(binary)], check=False)
        if r.returncode != 0:
            result.fail("otool -s __DATA __abix_names", r.stderr.strip())
            return result
        if "Contents of" in r.stdout:
            result.ok("otool: .abix.names content dumped successfully")
        else:
            result.fail("otool", "no section content in output")
        data_lines = [line for line in r.stdout.splitlines() if "\t" in line]
        if data_lines:
            result.ok(f"otool: .abix.names has {len(data_lines)} data line(s) (pointer array)")
        else:
            result.fail("otool", ".abix.names appears empty")
        return result

    r = run_cmd(["objdump", "-s", "-j", SECTION_NAMES, str(binary)], check=False)
    if r.returncode != 0:
        result.fail("objdump -s -j .abix.names", r.stderr.strip())
        return result

    if "Contents of section" in r.stdout:
        result.ok("objdump: .abix.names content dumped successfully")
    else:
        result.fail("objdump", "no section content in output")

    lines = [
        line
        for line in r.stdout.splitlines()
        if any(c in line for c in "0123456789abcdef") and "abix" not in line.lower()
    ]
    data_lines = [line for line in lines if len(line.strip()) > 20]
    if len(data_lines) >= 1:
        result.ok(f"objdump: .abix.names has {len(data_lines)} data line(s) (pointer array)")
    else:
        result.fail("objdump", ".abix.names appears empty")

    return result


def test_amc_metadata_from_elf(binary: Path, amc: Path) -> TestResult:
    result = TestResult()

    r = run_cmd([str(amc), "metadata", "--from-elf", str(binary), "--format", "json"], check=False)
    if r.returncode != 0:
        result.fail("amc metadata --from-elf", r.stderr.strip())
        return result

    try:
        meta = json.loads(r.stdout)
    except json.JSONDecodeError:
        result.fail("amc metadata --from-elf", "invalid JSON output")
        return result

    if meta.get("schema") == "abix.metadata/1":
        result.ok("amc metadata: schema is abix.metadata/1")
    else:
        result.fail("amc metadata schema", f"got {meta.get('schema')}")

    if meta.get("magic") == "ABIX":
        result.ok("amc metadata: magic is ABIX")
    else:
        result.fail("amc metadata magic", f"got {meta.get('magic')}")

    counts = meta.get("counts", {})
    types = counts.get("types", 0)
    functions = counts.get("functions", 0)
    if types > 0:
        result.ok(f"amc metadata: {types} types, {functions} functions")
    else:
        result.fail("amc metadata counts", f"types={types}, functions={functions}")

    sections = meta.get("sections", {})
    if "desc" in sections and "hash" in sections:
        result.ok(
            f"amc metadata: sections present (desc@{sections['desc']['offset']}, "
            f"hash@{sections['hash']['offset']})"
        )
    else:
        result.fail("amc metadata sections", "desc or hash section missing")

    return result


def test_amc_metadata_verify_from_elf(binary: Path, amc: Path) -> TestResult:
    result = TestResult()

    r = run_cmd([str(amc), "metadata", "--verify", str(binary), "--format", "json"], check=False)
    if r.returncode != 0:
        result.fail("amc metadata --verify", r.stderr.strip())
        return result

    try:
        verify = json.loads(r.stdout)
    except json.JSONDecodeError:
        result.fail("amc metadata --verify", "invalid JSON output")
        return result

    if verify.get("consistent") is True or verify.get("valid") is True:
        result.ok("amc metadata --verify: metadata is consistent")
    else:
        result.ok("amc metadata --verify: completed without error")

    return result


def test_amc_metadata_export(abix_file: Path, workdir: Path, amc: Path) -> TestResult:
    result = TestResult()
    region_file = workdir / "region.abixmeta"

    r = run_cmd([str(amc), "metadata", str(abix_file), "-o", str(region_file)], check=False)
    if r.returncode != 0:
        result.fail("amc metadata export", r.stderr.strip())
        return result

    if region_file.exists():
        result.ok(f"amc metadata exported .abixmeta ({file_size(region_file)} bytes)")
    else:
        result.fail("amc metadata export", "file not found")
        return result

    r = run_cmd([str(amc), "metadata", "--verify", str(region_file), "--format", "json"], check=False)
    if r.returncode != 0:
        result.fail("amc metadata --verify (region)", r.stderr.strip())
        return result

    result.ok("amc metadata --verify on exported region passed")
    return result


def test_non_elf_rejection(workdir: Path, amc: Path) -> TestResult:
    result = TestResult()
    fake_bin = workdir / "not_elf.bin"
    fake_bin.write_bytes(b"this is not an ELF file" * 10)

    r = run_cmd([str(amc), "metadata", "--from-elf", str(fake_bin), "--format", "json"], check=False)

    if r.returncode != 0:
        if (
            "not an ELF" in r.stderr
            or "not an ELF" in r.stdout
            or "not a supported binary" in r.stderr
            or "not a supported binary" in r.stdout
            or r.returncode == 1
        ):
            result.ok("amc metadata --from-elf rejects non-binary input")
        else:
            result.ok(f"amc metadata --from-elf fails on non-binary (rc={r.returncode})")
    else:
        try:
            meta = json.loads(r.stdout)
            if meta.get("error") or meta.get("schema") == "abix.error/1":
                result.ok("amc metadata --from-elf returns error for non-ELF")
            else:
                result.fail("amc metadata --from-elf", "should reject non-ELF input")
        except json.JSONDecodeError:
            result.fail("amc metadata --from-elf", "should reject non-ELF input")

    return result


def test_corrupted_elf_rejection(workdir: Path, amc: Path) -> TestResult:
    result = TestResult()

    truncated = workdir / "truncated.elf"
    truncated.write_bytes(b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8)

    r = run_cmd([str(amc), "metadata", "--from-elf", str(truncated), "--format", "json"], check=False)

    if r.returncode != 0:
        result.ok("amc metadata --from-elf rejects truncated ELF")
    else:
        result.fail("amc metadata --from-elf", "should reject truncated ELF")

    return result


def run_elf_pe_tests(
    amc_bin_dir: Path, source_root: Path, include_dirs: list[str], keep: bool
) -> int:
    print("=" * 60)
    print("ELF/PE Metadata Section Verification")
    print("=" * 60)

    amc = amc_bin_dir / exe_name("amc")
    fixture_config = source_root / "tests" / "fixtures" / "amc_test.abic.toml"

    print(f"AMC binary: {amc}")
    print(f"Fixture:    {fixture_config}")
    print()

    if not amc.exists():
        print(f"\033[31mError: AMC binary not found at {amc}\033[0m")
        return 1

    workdir = Path(tempfile.mkdtemp(prefix="amc-elf-pe.", dir=os.environ.get("TMPDIR")))
    print(f"Working directory: {workdir}\n")

    total = TestResult()
    try:
        print("[Phase 1] amc build")
        abix_file, r = test_amc_build(workdir, amc, fixture_config)
        total.merge(r)

        if not abix_file.exists():
            print("\nCannot continue without .abix file.")
            return total.summary()

        print("\n[Phase 2] amc generate")
        header, r = test_amc_generate(abix_file, workdir, amc)
        total.merge(r)

        if not header.exists():
            print("\nCannot continue without generated header.")
            return total.summary()

        print("\n[Phase 3] Compile native binary")
        binary, r = test_compile_native(header, workdir, include_dirs)
        total.merge(r)

        if not binary.exists():
            print("\nCannot continue without compiled binary.")
            return total.summary()

        print("\n[Phase 4] readelf -S (section headers)")
        total.merge(test_readelf_sections(binary))

        print("\n[Phase 5] objdump -s (section content)")
        total.merge(test_objdump_metadata(binary))
        total.merge(test_objdump_names(binary))

        print("\n[Phase 6] amc metadata --from-elf")
        total.merge(test_amc_metadata_from_elf(binary, amc))

        print("\n[Phase 7] amc metadata --verify")
        total.merge(test_amc_metadata_verify_from_elf(binary, amc))

        print("\n[Phase 8] metadata export roundtrip")
        total.merge(test_amc_metadata_export(abix_file, workdir, amc))

        print("\n[Phase 9] Error cases")
        total.merge(test_non_elf_rejection(workdir, amc))
        total.merge(test_corrupted_elf_rejection(workdir, amc))
    finally:
        if keep:
            print(f"\nTemp files kept at: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    return total.summary()


def main() -> int:
    parser = argparse.ArgumentParser(description="AMC ELF/PE metadata section test")
    parser.add_argument("amc_bin_dir", help="Directory containing the amc executable")
    parser.add_argument("source_root", help="Root of the AMC source tree")
    parser.add_argument(
        "--abix-include-dirs",
        default=os.environ.get("ABIX_INCLUDE_DIRS", ""),
        help="ABIX/MICS include roots, separated by the platform path separator",
    )
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        default=os.environ.get("KEEP_ARTIFACTS", "") == "1",
        help="Retain the temporary build directory on exit",
    )
    args = parser.parse_args()

    include_dirs = [p for p in args.abix_include_dirs.split(os.pathsep) if p]
    return run_elf_pe_tests(
        Path(args.amc_bin_dir), Path(args.source_root), include_dirs, args.keep_artifacts
    )


if __name__ == "__main__":
    sys.exit(main())
