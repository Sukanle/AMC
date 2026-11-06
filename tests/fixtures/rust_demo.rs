//! Fixture for the AMC Rust provider test.
//!
//! Only the FFI surface is exercised: `#[repr(C)]` aggregates and
//! `extern "C"` functions.

#[repr(C)]
pub struct RustPoint {
    pub x: i32,
    pub y: i32,
}

#[repr(C)]
pub struct RustRect {
    pub origin: RustPoint,
    pub width: f64,
    pub height: f64,
}

#[repr(C)]
pub enum RustColor {
    Red,
    Green,
    Blue,
}

#[repr(C)]
pub struct RustBuffer {
    pub data: *mut u8,
    pub len: usize,
}

#[repr(C)]
pub struct RustIntPtrs {
    pub signed: isize,
    pub unsigned: usize,
}

#[repr(C)]
pub struct RustArray {
    pub tag: u8,
    pub values: [u16; 3],
}

pub type RustHandle = *mut RustPoint;

#[repr(u8)]
pub enum RustSmallColor {
    Red = 1,
    Green = 2,
}

#[repr(C, align(16))]
pub struct RustAligned {
    pub value: u32,
}

extern "C" {
    pub fn rust_make_rect(x: i32, y: i32, w: f64, h: f64) -> RustRect;
    pub fn rust_rect_area(rect: *const RustRect) -> f64;
    pub fn rust_shade(color: RustColor) -> u32;
}

#[no_mangle]
pub extern "C" fn rust_buffer_len(buffer: *const RustBuffer) -> usize {
    0
}

#[no_mangle]
pub extern "C" fn rust_touch() {}
