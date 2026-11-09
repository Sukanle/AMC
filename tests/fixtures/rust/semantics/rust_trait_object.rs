//! Trait-object fixture for the rustc helper: `dyn Trait` is semantic
//! metadata, and AMC must reject it as an implicit ABI type.
//!
//! This file is consumed by `amc-rustc-helper` and by negative AMC tests; it is
//! not part of the ABI projection fixtures.

pub trait Named {
    fn name(&self) -> i32;
}

#[repr(C)]
pub struct DynPayload {
    pub value: i32,
}

#[repr(C)]
pub struct DynField {
    pub name: &'static dyn Named,
}

#[no_mangle]
pub extern "C" fn trait_object_name(value: &dyn Named) -> i32 {
    value.name()
}
