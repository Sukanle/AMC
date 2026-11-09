// Point cargo at the rustc-dev libraries that ship with the active toolchain.
//
// `rustc_private` crates are not regular dependencies: they live in the
// sysroot and must be found with `-L`. The build script queries the same rustc
// that cargo is about to invoke, so the helper is always built against the
// toolchain it will run under.

use std::process::Command;

fn rustc() -> String {
    std::env::var("RUSTC").unwrap_or_else(|_| "rustc".to_string())
}

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=RUSTC");

    let sysroot = Command::new(rustc())
        .args(["--print", "sysroot"])
        .output()
        .ok()
        .filter(|output| output.status.success())
        .map(|output| String::from_utf8_lossy(&output.stdout).trim().to_string())
        .expect("rustc --print sysroot failed");

    let version = Command::new(rustc())
        .arg("-vV")
        .output()
        .ok()
        .filter(|output| output.status.success())
        .map(|output| String::from_utf8_lossy(&output.stdout).into_owned())
        .expect("rustc -vV failed");
    let host = version
        .lines()
        .find_map(|line| line.strip_prefix("host:"))
        .map(str::trim)
        .expect("rustc -vV did not report a host triple");

    let driver_lib = format!("{sysroot}/lib");
    let driver_target_lib = format!("{sysroot}/lib/rustlib/{host}/lib");
    println!("cargo:rustc-link-search=native={driver_lib}");
    println!("cargo:rustc-link-search=native={driver_target_lib}");

    if !cfg!(windows) {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{driver_lib}");
        println!("cargo:rustc-link-arg=-Wl,-rpath,{driver_target_lib}");
    }
}
