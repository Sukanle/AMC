// Fixture for the explicit `[[rust.expose]]` handoff.
//
// `rust_expose_add` and `rust_expose_point_sum` are ordinary safe Rust
// functions; AMC emits the `extern "C"` wrappers. `rust_expose_direct` is
// already `extern "C"` and keeps its own symbol.

#[repr(C)]
pub struct RustExposePoint {
    pub x: i32,
    pub y: i32,
}

pub fn rust_expose_add(a: i32, b: i32) -> i32 {
    a + b
}

pub fn rust_expose_plain(value: i32) -> i32 {
    value
}

pub unsafe fn rust_expose_unsafe(value: i32) -> i32 {
    value
}

#[no_mangle]
pub extern "C" fn rust_expose_direct(value: i32) -> i32 {
    value
}

pub fn rust_expose_point_sum(point: RustExposePoint) -> i32 {
    point.x + point.y
}
