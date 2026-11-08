// Fixture for the target-aware `c_long` / `c_ulong` default.
//
// The same source is built twice with different `[target] os` values:
// Linux/macOS use the pointer width (LP64), Windows x64 uses 32-bit c_long
// (LLP64). The layout is checked through `amc query --layout`; no rustc
// cross-target toolchain is required.

use core::ffi::{c_long, c_ulong};

#[repr(C)]
pub struct RustCLongs {
    pub signed: c_long,
    pub unsigned: c_ulong,
}
