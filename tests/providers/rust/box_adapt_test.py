#!/usr/bin/env python3
"""Explicit `Box<T>` owned-handle test.

Usage:
    box_adapt_test.py <amc_bin_dir> [source_root]

Builds a Rust fixture whose ordinary functions take and return `Box<BoxWidget>`.
`[[rust.expose]]` + `[[adapt]]` turn the boxed value into an owned pointer, and
the explicit `[[owned]]` / `[[contract]]` declarations name the create/destroy
pair. The test compiles the provider, generates the Rust projection and runs a
consumer that exercises the RAII wrapper and ownership transfer.
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
        print("usage: box_adapt_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = Path(argv[0]).resolve()
    source_root = Path(argv[1]).resolve() if len(argv) > 1 else bin_dir.parent.parent
    fixtures = source_root / "tests" / "fixtures"
    if not fixtures.is_dir():
        fixtures = source_root / "AMC" / "tests" / "fixtures"
    amc = bin_dir / exe("amc")
    rustc = shutil.which("rustc")
    if not rustc:
        print("  [SKIP] rustc is required for the Box adaptation test")
        return 0

    work = tempfile.mkdtemp(prefix="amc-rust-box.")
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
        for name in ("rust_box_api.rs", "rust_box_api.abic.toml"):
            shutil.copyfile(os.path.join(fixtures, "rust", "containers", name), os.path.join(work, name))
        config = os.path.join(work, "rust_box_api.abic.toml")
        out = os.path.join(work, "out")
        artifact = os.path.join(out, "rust_box_api.abix")

        result = run([str(amc), "build", "-c", config, "-B", out])
        if not check(result.returncode == 0 and os.path.isfile(artifact),
                     "explicit Box owned handle built", output(result)[-600:]):
            return 1

        thunks = open(os.path.join(work, "box_thunks.rs"), encoding="utf-8").read()
        check("Box::into_raw(crate::box_widget_make" in thunks,
              "Box return becomes an owned raw handle")
        check("Box::from_raw(p0)" in thunks, "Box parameter takes ownership")
        check("-> *mut BoxWidget" in thunks, "wrapper return type is the owned pointer")

        for function, expected_kind in (("box_widget_make", "pointer"), ("box_widget_value", "primitive")):
            result = run([str(amc), "query", artifact, "--function", function, "--format", "json"])
            if check(result.returncode == 0, f"{function} is recorded", output(result)[-300:]):
                match = json.loads(result.stdout).get("matches", [{}])[0]
                if function == "box_widget_make":
                    check(match.get("return_type_name", "").startswith("*mut")
                          and "BoxWidget" in match.get("return_type_name", ""),
                          "create returns the owned pointer type")
                else:
                    parameter = (match.get("parameters") or [{}])[0]
                    check(parameter.get("type_name", "").startswith("*mut")
                          and "BoxWidget" in parameter.get("type_name", ""),
                          "consume takes the owned pointer type")

        gen = os.path.join(work, "gen")
        result = run([str(amc), "generate", artifact, "-l", "rust", "-o", gen])
        if not check(result.returncode == 0, "Rust projection generated", output(result)[-400:]):
            return 1
        projection = open(os.path.join(gen, "amc_generated.rs"), encoding="utf-8").read()
        check("pub struct OwnedBoxWidget" in projection, "owned Box type becomes an RAII wrapper")
        check("box_widget_destroy(self.ptr)" in projection, "RAII wrapper calls the declared destroy")

        provider_root = os.path.join(work, "provider.rs")
        with open(provider_root, "w", encoding="utf-8") as handle:
            handle.write('include!("rust_box_api.rs");\n')
            handle.write('include!("box_thunks.rs");\n')
        provider = module_path(work, "rust_box_api")
        result = run([rustc, "--edition", "2021", "--crate-type", "cdylib",
                      provider_root, "-o", provider])
        if not check(result.returncode == 0, "native Box provider compiled", output(result)[-500:]):
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
        assert_eq!(box_widget_live(), 0);
        let owned = OwnedBoxWidget::create(7).expect("create");
        assert_eq!(owned.as_ref().value, 7);
        assert_eq!(box_widget_live(), 1);
        let ptr = owned.into_raw();
        assert_eq!(box_widget_value(ptr), 7);
        assert_eq!(box_widget_live(), 0, "Box transfer consumed the value");
    }
    println!("rust box: OK");
}
""")
        consumer_bin = os.path.join(work, exe("consumer"))
        link = ["-L", "native=" + work, "-l", "dylib=rust_box_api"]
        if sys.platform != "win32":
            link += ["-C", "link-arg=-Wl,-rpath," + work]
        result = run([rustc, "--edition", "2021", consumer, "-o", consumer_bin] + link)
        if check(result.returncode == 0, "Box consumer compiled", output(result)[-500:]):
            env = dict(os.environ)
            if sys.platform == "win32":
                env["PATH"] = work + os.pathsep + env.get("PATH", "")
            else:
                env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
            result = run([consumer_bin], env=env)
            check(result.returncode == 0 and "rust box: OK" in output(result),
                  "consumer drives create, transfer and destroy", output(result)[-500:])

        # Optional auto thunks: create = "auto" / destroy = "auto" synthesize
        # the Box::new and Box::from_raw pair, while [[owned]]/[[contract]]
        # still declare the policy.
        auto_dir = os.path.join(work, "auto")
        os.makedirs(auto_dir, exist_ok=True)
        shutil.copyfile(os.path.join(work, "rust_box_api.rs"), os.path.join(auto_dir, "rust_box_api.rs"))
        auto_config = os.path.join(auto_dir, "rust_box_auto.abic.toml")
        with open(auto_config, "w", encoding="utf-8") as handle:
            handle.write("""[package]
name = "rust_box_auto"
version = "0.1"

[[import]]
language = "rust"
files = ["rust_box_api.rs"]
symbols = ["BoxWidget", "box_widget_live"]

[[adapt]]
native = "Box"
target = "*mut BoxWidget"

[[owned]]
type = "BoxWidget"
create = "auto"
destroy = "auto"

[[contract]]
function = "box_widget_make"
returns = "owned"
release = "auto"

[[contract]]
function = "box_widget_value"
params = ["owned"]

[[rust.expose]]
function = "box_widget_make"
as = "box_widget_make"

[[rust.expose]]
function = "box_widget_value"
as = "box_widget_value"

[target]
arch = 0
os = 0
abi = 1
pointer_bits = 64
endianness = "little"

[thunks]
output = "box_auto_thunks.rs"

[[export]]
output = "rust_box_auto.abix"
""")
        result = run([str(amc), "build", "-c", auto_config, "-B", os.path.join(auto_dir, "out")])
        auto_artifact = os.path.join(auto_dir, "out", "rust_box_auto.abix")
        if check(result.returncode == 0 and os.path.isfile(auto_artifact),
                 "auto create/destroy thunks build", output(result)[-600:]):
            auto_thunks = open(os.path.join(auto_dir, "box_auto_thunks.rs"), encoding="utf-8").read()
            check('export_name = "__amc_auto_create_BoxWidget"' in auto_thunks
                  and "Box::into_raw(Box::new(value))" in auto_thunks,
                  "auto create thunk is generated")
            check('export_name = "__amc_auto_destroy_BoxWidget"' in auto_thunks
                  and "Box::from_raw(ptr)" in auto_thunks,
                  "auto destroy thunk is generated")
            auto_gen = os.path.join(auto_dir, "gen")
            result = run([str(amc), "generate", auto_artifact, "-l", "rust", "-o", auto_gen])
            if check(result.returncode == 0, "auto Box Rust projection generated", output(result)[-400:]):
                auto_projection = open(os.path.join(auto_gen, "amc_generated.rs"), encoding="utf-8").read()
                check("pub fn create(value: BoxWidget)" in auto_projection,
                      "auto create keeps the value parameter")
                auto_root = os.path.join(auto_dir, "provider.rs")
                with open(auto_root, "w", encoding="utf-8") as handle:
                    handle.write('include!("rust_box_api.rs");\n')
                    handle.write('include!("box_auto_thunks.rs");\n')
                auto_provider = module_path(auto_dir, "rust_box_auto")
                result = run([rustc, "--edition", "2021", "--crate-type", "cdylib",
                              auto_root, "-o", auto_provider])
                if check(result.returncode == 0, "auto Box provider compiled", output(result)[-500:]):
                    auto_consumer = os.path.join(auto_dir, "consumer.rs")
                    with open(auto_consumer, "w", encoding="utf-8") as handle:
                        handle.write("""#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}
use skl::abix::*;

fn main() {
    unsafe {
        let owned = OwnedBoxWidget::create(BoxWidget { value: 9 }).expect("create");
        assert_eq!(owned.as_ref().value, 9);
        drop(owned);
    }
    println!("rust box auto: OK");
}
""")
                    auto_bin = os.path.join(auto_dir, exe("consumer"))
                    link = ["-L", "native=" + auto_dir, "-l", "dylib=rust_box_auto"]
                    if sys.platform != "win32":
                        link += ["-C", "link-arg=-Wl,-rpath," + auto_dir]
                    result = run([rustc, "--edition", "2021", auto_consumer, "-o", auto_bin] + link)
                    if check(result.returncode == 0, "auto Box consumer compiled", output(result)[-500:]):
                        env = dict(os.environ)
                        if sys.platform == "win32":
                            env["PATH"] = auto_dir + os.pathsep + env.get("PATH", "")
                        else:
                            env["LD_LIBRARY_PATH"] = auto_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                            env["DYLD_LIBRARY_PATH"] = auto_dir + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
                        result = run([auto_bin], env=env)
                        check(result.returncode == 0 and "rust box auto: OK" in output(result),
                              "auto create/destroy pair runs", output(result)[-500:])

        # Explicit means explicit: dropping [[owned]] or [[contract]] fails closed.
        no_owned = os.path.join(work, "no_owned.abic.toml")
        with open(config, encoding="utf-8") as handle:
            text = handle.read()
        text = text.replace("""[[owned]]
type = "BoxWidget"
create = "box_widget_make"
destroy = "box_widget_destroy"

""", "")
        with open(no_owned, "w", encoding="utf-8") as handle:
            handle.write(text)
        result = run([str(amc), "build", "-c", no_owned, "-B", os.path.join(work, "no_owned_out")])
        no_owned_output = output(result)
        check(result.returncode != 0 and "requires [[owned]]" in no_owned_output,
              "Box mapping without [[owned]] fails closed", no_owned_output[-500:])

        no_return_contract = os.path.join(work, "no_return_contract.abic.toml")
        with open(config, encoding="utf-8") as handle:
            text = handle.read()
        text = text.replace("""[[contract]]
function = "box_widget_make"
returns = "owned"
release = "box_widget_destroy"

""", "")
        with open(no_return_contract, "w", encoding="utf-8") as handle:
            handle.write(text)
        result = run([str(amc), "build", "-c", no_return_contract,
                      "-B", os.path.join(work, "no_return_contract_out")])
        no_return_output = output(result)
        check(result.returncode != 0 and "returns = \"owned\"" in no_return_output,
              "Box return without an owned contract fails closed", no_return_output[-500:])

        no_contract = os.path.join(work, "no_contract.abic.toml")
        with open(config, encoding="utf-8") as handle:
            text = handle.read()
        text = text.replace("""[[contract]]
function = "box_widget_value"
params = ["owned"]

""", "")
        with open(no_contract, "w", encoding="utf-8") as handle:
            handle.write(text)
        result = run([str(amc), "build", "-c", no_contract, "-B", os.path.join(work, "no_contract_out")])
        no_contract_output = output(result)
        check(result.returncode != 0 and "params[0] = \"owned\"" in no_contract_output,
              "Box parameter without an owned contract fails closed", no_contract_output[-500:])

        if keep:
            print("  kept artifacts in " + work)
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)

    print("  %d passed, %d failed" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
