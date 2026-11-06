// Layout-only fixture for advanced repr forms that the backends do not emit yet.
#[repr(C, packed(2))]
pub struct RustPacked2 {
    pub kind: u8,
    pub value: u32,
}

#[repr(C)]
pub union RustUnion {
    pub word: u32,
    pub wide: u64,
}
