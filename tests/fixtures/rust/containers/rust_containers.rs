//! Standard-container fixture for the rustc helper: Option, Box, String and
//! Vec are semantic facts that require an explicit ABI contract.
//!
//! This file is consumed by `amc-rustc-helper` and by negative AMC tests; it is
//! not part of the ABI projection fixtures.

#[repr(C)]
pub struct ContainerPayload {
    pub value: i32,
}

#[no_mangle]
pub extern "C" fn container_option(value: Option<i32>) -> i32 {
    value.unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn container_box(value: Box<ContainerPayload>) -> i32 {
    value.value
}

#[no_mangle]
pub extern "C" fn container_string(value: String) -> usize {
    value.len()
}

#[no_mangle]
pub extern "C" fn container_vec(value: Vec<u8>) -> usize {
    value.len()
}
