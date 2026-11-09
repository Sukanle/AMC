#!/usr/bin/env python3
"""`amc-rust` provider conformance run.

Usage:
    conformance_test.py <amc_bin_dir> [source_root]

The generic provider-contract steps live in `conformance/provider_harness.py`;
this file only declares the Rust provider's fixture and expected ABI facts.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "conformance")))

from provider_harness import ProviderCase, ProviderConformance


def rust_compile(tool: str, projection: str, out_dir: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, "--edition", "2021", "--crate-type=lib", "--emit=metadata",
         "--out-dir", out_dir, projection],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )


def make_case(source_root: str) -> ProviderCase:
    fixtures = os.path.join(source_root, "tests", "fixtures")
    if not os.path.isdir(fixtures):
        fixtures = os.path.join(source_root, "AMC", "tests", "fixtures")
    return ProviderCase(
        language="rust",
        config=os.path.join(fixtures, "rust", "demo", "rust_demo.abic.toml"),
        artifact_name="rust_demo.abix",
        expected_functions=5,
        layout_checks=[
            ("RustRect", "RustRect::origin offset = 0", ["origin", "offset=0"]),
            ("RustRect", "RustRect::width offset = 8", ["width", "offset=8"]),
            ("RustRect", "RustRect::height offset = 16", ["height", "offset=16"]),
            ("RustBuffer", "RustBuffer::data offset = 0", ["data", "offset=0"]),
            ("RustBuffer", "RustBuffer::len follows the target pointer width",
             ["len", "offset=8"]),
            ("RustIntPtrs", "RustIntPtrs::signed offset = 0", ["signed", "offset=0"]),
            ("RustIntPtrs", "RustIntPtrs::unsigned follows the target pointer width",
             ["unsigned", "offset=8"]),
            ("RustArray", "RustArray uses C field alignment", ["size=8", "align=2"]),
            ("RustArray", "RustArray::values offset = 2", ["values", "offset=2"]),
            ("RustTransparent", "repr(transparent) matches its field", ["size=4", "align=4"]),
            ("RustTransparent", "RustTransparent::code offset = 0", ["code", "offset=0"]),
            ("RustHandle", "RustHandle is a target-sized alias", ["size=8", "align=8"]),
            ("RustSmallColor", "repr(u8) enum is one byte", ["size=1", "align=1"]),
            ("RustAligned", "repr(C, align(16)) overrides the alignment",
             ["size=16", "align=16"]),
        ],
        projection_name="amc_generated.rs",
        projection_checks=[
            ("projection uses repr(C)", ["#[repr(C)]"]),
            ("C int projects to core::ffi::c_int", ["core::ffi::c_int"]),
            ("C double projects to core::ffi::c_double", ["core::ffi::c_double"]),
            ("projection emits offset assertions", ["offset_of!(RustRect, width)"]),
            ("pointer-sized aliases keep isize", ["pub signed: isize"]),
            ("pointer-sized aliases keep usize", ["pub unsigned: usize"]),
            ("projection embeds the ABIX metadata region", ["AMC_METADATA_REGION"]),
            ("raw FFI declarations are isolated", ["pub mod raw", "unsafe extern \"C\""]),
            ("unsafe calls go through one macro", ["macro_rules! amc_ffi", "unsafe { raw::"]),
            ("value-only functions get safe wrappers", ["pub fn rust_make_rect("]),
            ("pointer functions stay unsafe", ["pub unsafe fn rust_rect_area("]),
        ],
        compile_tool="rustc",
        compile=rust_compile,
    )


def main() -> int:
    if "--keep-artifacts" in sys.argv:
        os.environ["KEEP_ARTIFACTS"] = "1"
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    if not argv:
        print(__doc__)
        return 2
    bin_dir = argv[0]
    source_root = argv[1] if len(argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", ".."))
    return ProviderConformance(bin_dir, make_case(source_root)).run_all()


if __name__ == "__main__":
    sys.exit(main())
