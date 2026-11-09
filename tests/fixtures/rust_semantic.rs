//! Semantic fixture for the rustc helper: nested modules, impl methods,
//! generics, traits and `Result` error types.
//!
//! This file is consumed by `amc-rustc-helper` directly; it is not part of the
//! AMC ABI projection fixtures.

pub trait Shape {
    type Unit;

    fn area(&self) -> f64;
    fn perimeter(&self) -> f64 {
        0.0
    }
}

#[repr(C)]
pub struct Circle {
    pub radius: f64,
}

impl Shape for Circle {
    type Unit = f64;

    fn area(&self) -> f64 {
        3.141592653589793 * self.radius * self.radius
    }
}

#[repr(C)]
pub struct Wrapper<T> {
    pub value: T,
}

pub type WrapperI32 = Wrapper<i32>;

pub mod nested {
    #[repr(C)]
    pub struct NestedPoint {
        pub x: i32,
        pub y: i32,
    }

    impl NestedPoint {
        #[no_mangle]
        pub extern "C" fn nested_sum(point: *const NestedPoint) -> i32 {
            if point.is_null() {
                return 0;
            }
            unsafe { (*point).x + (*point).y }
        }
    }
}

#[repr(u32)]
pub enum RustFailure {
    None = 0,
    InvalidInput = 1,
    Overflow = 2,
}

#[no_mangle]
pub extern "C" fn rust_try_fallible(value: i32) -> Result<i32, RustFailure> {
    if value < 0 {
        Err(RustFailure::InvalidInput)
    } else {
        Ok(value)
    }
}

#[no_mangle]
pub extern "C" fn rust_try_nested(point: *const nested::NestedPoint) -> Result<i32, RustFailure> {
    if point.is_null() {
        return Err(RustFailure::None);
    }
    Ok(unsafe { (*point).x })
}
