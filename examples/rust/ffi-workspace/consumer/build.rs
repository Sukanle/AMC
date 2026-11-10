fn main() {
    let provider = std::env::var("AMC_PROVIDER_DIR")
        .unwrap_or_else(|_| format!("{}/../../target/release", env!("CARGO_MANIFEST_DIR")));
    println!("cargo:rustc-link-search=native={provider}");
    println!("cargo:rustc-link-lib=dylib=amc_demo_provider");
    println!("cargo:rerun-if-env-changed=AMC_PROVIDER_DIR");
}
