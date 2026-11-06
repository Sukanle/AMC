// Layout-only fixture: packed struct parity is checked against rustc before the
// backends grow explicit packed emission.
#[repr(C, packed)]
pub struct RustPacked {
    pub kind: u8,
    pub value: u32,
}
