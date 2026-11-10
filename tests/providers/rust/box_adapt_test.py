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

        # ABIX v0.4.0 owner handle: Box<T> maps to abix_owner_T with the
        # canonical object/mode/flags/destroy/module_instance layout.
        owner_dir = os.path.join(work, "owner")
        os.makedirs(owner_dir, exist_ok=True)
        shutil.copyfile(os.path.join(work, "rust_box_api.rs"), os.path.join(owner_dir, "rust_box_api.rs"))
        owner_config = os.path.join(owner_dir, "rust_box_owner.abic.toml")
        with open(owner_config, "w", encoding="utf-8") as handle:
            handle.write("""[package]
name = "rust_box_owner"
version = "0.1"

[[import]]
language = "rust"
files = ["rust_box_api.rs"]
symbols = ["BoxWidget", "box_widget_live"]

[[adapt]]
native = "Box"
target = "abix_owner_BoxWidget"

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
output = "box_owner_thunks.rs"

[[export]]
output = "rust_box_owner.abix"
""")
        result = run([str(amc), "build", "-c", owner_config, "-B", os.path.join(owner_dir, "out")])
        owner_artifact = os.path.join(owner_dir, "out", "rust_box_owner.abix")
        if check(result.returncode == 0 and os.path.isfile(owner_artifact),
                 "owner handle Box build", output(result)[-600:]):
            owner_thunks = open(os.path.join(owner_dir, "box_owner_thunks.rs"), encoding="utf-8").read()
            check("pub struct abix_owner_BoxWidget" in owner_thunks
                  and "mode: 1" in owner_thunks
                  and "Box::into_raw(__amc_value)" in owner_thunks,
                  "owner handle return is generated")
            check("Box::from_raw(p0.object as *mut BoxWidget)" in owner_thunks,
                  "owner handle parameter is unwrapped")
            check("__amc_owner_destroy_BoxWidget" in owner_thunks,
                  "owner handle destroy trampoline is generated")
            for function, expected in (("box_widget_make", "abix_owner_BoxWidget"),
                                       ("box_widget_value", "abix_owner_BoxWidget")):
                result = run([str(amc), "query", owner_artifact, "--function", function,
                              "--format", "json"])
                if check(result.returncode == 0, f"owner {function} is recorded", output(result)[-300:]):
                    match = json.loads(result.stdout).get("matches", [{}])[0]
                    if function == "box_widget_make":
                        check(match.get("return_type_name") == expected,
                              "owner make returns the owner handle")
                    else:
                        parameter = (match.get("parameters") or [{}])[0]
                        check(parameter.get("type_name") == expected,
                              "owner consume takes the owner handle")
            owner_gen = os.path.join(owner_dir, "gen")
            result = run([str(amc), "generate", owner_artifact, "-l", "rust", "-o", owner_gen])
            if check(result.returncode == 0, "owner Box Rust projection generated", output(result)[-400:]):
                owner_projection = open(os.path.join(owner_gen, "amc_generated.rs"), encoding="utf-8").read()
                check("pub struct OwnerOfBoxWidget" in owner_projection
                      and "impl Drop for OwnerOfBoxWidget" in owner_projection,
                      "owner handle RAII wrapper is generated")

            owner_root = os.path.join(owner_dir, "provider.rs")
            with open(owner_root, "w", encoding="utf-8") as handle:
                handle.write('include!("rust_box_api.rs");\n')
                handle.write('include!("box_owner_thunks.rs");\n')
            owner_provider = module_path(owner_dir, "rust_box_owner")
            result = run([rustc, "--edition", "2021", "--crate-type", "cdylib",
                          owner_root, "-o", owner_provider])
            if check(result.returncode == 0, "owner Box provider compiled", output(result)[-500:]):
                owner_consumer = os.path.join(owner_dir, "consumer.py")
                with open(owner_consumer, "w", encoding="utf-8") as handle:
                    handle.write("""import ctypes, os, sys
class Owner(ctypes.Structure):
    _fields_ = [("object", ctypes.c_void_p), ("mode", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                ("destroy", ctypes.c_void_p), ("module_instance", ctypes.c_uint64)]
lib = ctypes.CDLL(os.path.join(os.path.dirname(__file__), 'lib' + 'rust_box_owner' + ('.dylib' if sys.platform == 'darwin' else '.so')))
lib.box_widget_make.argtypes = [ctypes.c_int32]
lib.box_widget_make.restype = Owner
lib.box_widget_value.argtypes = [Owner]
lib.box_widget_value.restype = ctypes.c_int32
lib.box_widget_live.restype = ctypes.c_size_t
handle = lib.box_widget_make(7)
assert handle.object and handle.mode == 1 and handle.destroy
assert lib.box_widget_live() == 1
assert lib.box_widget_value(handle) == 7
assert lib.box_widget_live() == 0
handle2 = lib.box_widget_make(9)
ctypes.CFUNCTYPE(None, ctypes.c_void_p)(handle2.destroy)(handle2.object)
assert lib.box_widget_live() == 0
print('owner box: OK')
""")
                env = dict(os.environ)
                env["LD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                env["DYLD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
                result = run([sys.executable, owner_consumer], env=env)
                check(result.returncode == 0 and "owner box: OK" in output(result),
                      "owner handle create/consume/destroy runs", output(result)[-500:])

            cpp_header = os.path.join(owner_dir, "owner_gen.hpp")
            result = run([str(amc), "generate", owner_artifact, "-l", "cpp", "-o", cpp_header])
            if check(result.returncode == 0, "owner handle C++ projection generated", output(result)[-400:]):
                header = open(cpp_header, encoding="utf-8").read()
                check("class OwnerOfBoxWidget" in header,
                      "C++ owner handle RAII wrapper is generated")
                cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
                if cxx:
                    include_flags = [
                        "-I" + owner_dir,
                        "-I" + str(source_root / "include"),
                        "-I" + str(source_root / "dependencies" / "ABIX" / "include"),
                        "-I" + str(source_root / "dependencies" / "ABIX" / "MICS" / "include"),
                        "-I" + str(source_root / "dependencies" / "ABIX" / "MICS" / "HLMD"),
                    ]
                    cpp_consumer = os.path.join(owner_dir, "owner_consumer.cpp")
                    with open(cpp_consumer, "w", encoding="utf-8") as handle:
                        handle.write("""#include "owner_gen.hpp"

extern "C" {
::skl::amc::generated::abix_owner_BoxWidget box_widget_make(int32_t value);
int32_t box_widget_value(::skl::amc::generated::abix_owner_BoxWidget widget);
size_t box_widget_live();
}

int main() {
    auto owner = ::skl::amc::generated::OwnerOfBoxWidget(box_widget_make(7));
    if (!owner || owner.object() == nullptr) return 1;
    if (box_widget_live() != 1) return 2;
    if (box_widget_value(owner.release()) != 7) return 3;
    if (box_widget_live() != 0) return 4;
    {
        auto second = ::skl::amc::generated::OwnerOfBoxWidget(box_widget_make(9));
        (void)second;
    }
    if (box_widget_live() != 0) return 5;
    return 0;
}
""")
                    result = run([cxx, "-std=c++17", "-fsyntax-only"] + include_flags + [cpp_consumer])
                    check(result.returncode == 0, "C++ owner handle projection compiles", output(result)[-500:])
                    if sys.platform != "win32":
                        cpp_consumer_bin = os.path.join(owner_dir, exe("consumer_cpp"))
                        link = ["-L" + owner_dir, "-lrust_box_owner", "-Wl,-rpath," + owner_dir]
                        result = run([cxx, "-std=c++17"] + include_flags + [cpp_consumer, "-o", cpp_consumer_bin] + link)
                        if check(result.returncode == 0, "C++ owner handle consumer linked", output(result)[-500:]):
                            env = dict(os.environ)
                            env["LD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                            env["DYLD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
                            result = run([cpp_consumer_bin], env=env)
                            check(result.returncode == 0,
                                  "C++ owner handle RAII releases through destroy", output(result)[-500:])

            owner_rs = os.path.join(owner_dir, "consumer.rs")
            with open(owner_rs, "w", encoding="utf-8") as handle:
                handle.write("""#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}
use skl::abix::*;

fn main() {
    unsafe {
        let owner = OwnerOfBoxWidget::from_raw(box_widget_make(7)).expect("owner");
        assert_eq!(box_widget_live(), 1);
        assert_eq!(box_widget_value(owner.into_raw()), 7);
        assert_eq!(box_widget_live(), 0);

        let owner = OwnerOfBoxWidget::from_raw(box_widget_make(9)).expect("owner");
        drop(owner);
        assert_eq!(box_widget_live(), 0);
    }
    println!("owner box raii: OK");
}
""")
            owner_rs_bin = os.path.join(owner_dir, exe("consumer_rs"))
            link = ["-L", "native=" + owner_dir, "-l", "dylib=rust_box_owner"]
            if sys.platform != "win32":
                link += ["-C", "link-arg=-Wl,-rpath," + owner_dir]
            result = run([rustc, "--edition", "2021", owner_rs, "-o", owner_rs_bin] + link)
            if check(result.returncode == 0, "owner Box Rust consumer compiled", output(result)[-500:]):
                env = dict(os.environ)
                if sys.platform == "win32":
                    env["PATH"] = owner_dir + os.pathsep + env.get("PATH", "")
                else:
                    env["LD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                    env["DYLD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
                result = run([owner_rs_bin], env=env)
                check(result.returncode == 0 and "owner box raii: OK" in output(result),
                      "owner handle RAII wrapper releases through destroy", output(result)[-500:])

            borrowed_rs = os.path.join(owner_dir, "borrowed_consumer.rs")
            with open(borrowed_rs, "w", encoding="utf-8") as handle:
                handle.write("""#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}
use skl::abix::*;

fn main() {
    let mut raw = unsafe { box_widget_make(7) };
    raw.mode = 0;
    let _ = unsafe { box_widget_value(raw) };
    println!("borrowed owner was accepted");
}
""")
            borrowed_bin = os.path.join(owner_dir, exe("borrowed_consumer"))
            result = run([rustc, "--edition", "2021", borrowed_rs, "-o", borrowed_bin] + link)
            if check(result.returncode == 0, "borrowed owner consumer compiled", output(result)[-500:]):
                env = dict(os.environ)
                env["LD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
                env["DYLD_LIBRARY_PATH"] = owner_dir + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
                result = run([borrowed_bin], env=env)
                check(result.returncode != 0 and "must be owned or copied" in output(result),
                      "owner handle parameter rejects borrowed mode", output(result)[-500:])

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

        # A Box<dyn Trait> is a fat pointer and cannot use the thin owned handle.
        dyn_config = os.path.join(work, "box_dyn.abic.toml")
        with open(dyn_config, "w", encoding="utf-8") as handle:
            handle.write("""[package]
name = "rust_box_dyn"
version = "0.1"

[[import]]
language = "rust"
files = ["rust_box_api.rs"]
symbols = ["BoxWidget", "box_widget_destroy", "box_widget_live"]

[[adapt]]
native = "Box"
target = "*mut BoxWidget"

[[rust.expose]]
function = "box_dyn_make"
as = "box_dyn_make"

[target]
arch = 0
os = 0
abi = 1
pointer_bits = 64
endianness = "little"

[thunks]
output = "box_dyn_thunks.rs"

[[export]]
output = "rust_box_dyn.abix"
""")
        result = run([str(amc), "build", "-c", dyn_config, "-B", os.path.join(work, "box_dyn_out")])
        dyn_output = output(result)
        check(result.returncode != 0 and "fat pointer" in dyn_output,
              "Box<dyn Trait> is rejected as a fat pointer", dyn_output[-500:])

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
