#!/usr/bin/env python3
"""Rust ownership and error-boundary contract handoff test.

Usage:
    contract_handoff_test.py <amc_bin_dir> [source_root]

Builds a Rust fixture with `[[owned]]`, `[[contract]]` and `[[error]]`
declarations, generates the Rust projection, compiles the native provider and a
consumer, and verifies the RAII wrapper destroys the value and the error
wrapper reports success/failure.
"""

import os
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
    if sys.platform == "win32":
        return os.path.join(work, name + ".dll")
    return os.path.join(work, "lib" + name + ".so")


def main():
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    keep = "--keep-artifacts" in sys.argv[1:] or os.environ.get("KEEP_ARTIFACTS", "") == "1"
    if not argv:
        print("usage: contract_handoff_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "tests" / "fixtures"
    if not fixtures.is_dir():
        fixtures = source_root / "AMC" / "tests" / "fixtures"
    amc = bin_dir / exe("amc")
    rustc = shutil.which("rustc")
    if not rustc:
        print("  [SKIP] rustc is required for the contract handoff test")
        return 0

    work = tempfile.mkdtemp(prefix="amc-rust-contracts.")
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
        return subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", **kwargs)

    def output(r):
        return (r.stdout or "") + (r.stderr or "")

    try:
        for name in ("rust_contracts.rs", "rust_contracts.abic.toml"):
            shutil.copyfile(os.path.join(fixtures, name), os.path.join(work, name))
        config = os.path.join(work, "rust_contracts.abic.toml")
        out = os.path.join(work, "out")
        artifact = os.path.join(out, "rust_contracts.abix")

        r = run([str(amc), "build", "-c", config, "-B", out])
        if not check(r.returncode == 0 and os.path.isfile(artifact), "contract artifact built", output(r)[-400:]):
            return 1

        gen = os.path.join(work, "gen")
        r = run([str(amc), "generate", artifact, "-l", "rust", "-o", gen])
        if not check(r.returncode == 0, "Rust projection generated", output(r)[-400:]):
            return 1

        projection = os.path.join(gen, "amc_generated.rs")
        text = open(projection, encoding="utf-8").read() if os.path.isfile(projection) else ""
        check("pub struct OwnedRustOwnedBuffer" in text, "owned type becomes an RAII wrapper")
        check("rust_owned_destroy(self.ptr)" in text, "owned wrapper calls the declared destroy")
        check("pub fn rust_div_result(" in text, "error boundary becomes a typed Result")
        check("RustDivError" in text, "error wrapper has a named status type")

        provider = module_path(work, "rust_contracts_native")
        r = run(
            [
                rustc,
                "--edition",
                "2021",
                "--crate-type",
                "cdylib",
                os.path.join(work, "rust_contracts.rs"),
                "-o",
                provider,
            ]
        )
        if not check(r.returncode == 0, "native provider compiled", output(r)[-400:]):
            return 1

        consumer = os.path.join(work, "consumer.rs")
        with open(consumer, "w", encoding="utf-8") as handle:
            handle.write("""#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}
use skl::abix::*;

fn main() {
    unsafe {
        assert_eq!(rust_owned_live(), 0, "no live value before create");
        let owned = OwnedRustOwnedBuffer::create().expect("create");
        assert_eq!(rust_owned_live(), 1, "create is tracked");
        assert_eq!(owned.as_ref().len, 4);
        assert_eq!(*owned.as_ref().data, 1);

        assert_eq!(rust_div_result(10, 2), Ok(5));
        let err = rust_div_result(10, 0).expect_err("div zero");
        assert_eq!(err.0, 1);

        drop(owned);
        assert_eq!(rust_owned_live(), 0, "Drop calls the declared destroy");
    }
    println!("rust contracts: OK");
}
""")

        consumer_bin = os.path.join(work, exe("consumer"))
        link = ["-L", "native=" + work, "-l", "dylib=rust_contracts_native"]
        if sys.platform != "win32":
            link += ["-C", "link-arg=-Wl,-rpath," + work]
        r = run([rustc, "--edition", "2021", consumer, "-o", consumer_bin] + link)
        if check(r.returncode == 0, "consumer compiled", output(r)[-400:]):
            env = dict(os.environ)
            if sys.platform == "win32":
                env["PATH"] = work + os.pathsep + env.get("PATH", "")
            else:
                env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
            r = run([consumer_bin], env=env)
            check(
                r.returncode == 0 and "rust contracts: OK" in output(r),
                "consumer runs the RAII and error wrappers",
                output(r)[-400:],
            )

        # Negative: [[owned]] references an unknown type.
        bad_owned = os.path.join(work, "bad_owned.abic.toml")
        with open(config, encoding="utf-8") as handle:
            bad_owned_text = handle.read().replace('type = "RustOwnedBuffer"', 'type = "RustMissing"', 1)
        with open(bad_owned, "w", encoding="utf-8") as handle:
            handle.write(bad_owned_text)
        r = run([str(amc), "build", "-c", bad_owned, "-B", os.path.join(work, "bad_owned_out")])
        check(
            r.returncode != 0 and "unknown type" in output(r),
            "owned contract with an unknown type is rejected",
            output(r)[-400:],
        )

        # Negative: an owned return must name a release entry.
        bad_contract = os.path.join(work, "bad_contract.abic.toml")
        with open(config, encoding="utf-8") as handle:
            bad_contract_text = handle.read().replace(
                'returns = "owned"\nrelease = "rust_owned_destroy"', 'returns = "owned"', 1
            )
        with open(bad_contract, "w", encoding="utf-8") as handle:
            handle.write(bad_contract_text)
        r = run([str(amc), "build", "-c", bad_contract, "-B", os.path.join(work, "bad_contract_out")])
        check(
            r.returncode != 0 and "requires a release function" in output(r),
            "owned return without a release entry is rejected",
            output(r)[-400:],
        )

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
