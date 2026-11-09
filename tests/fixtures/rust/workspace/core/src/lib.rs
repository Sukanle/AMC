#[repr(C)]
pub struct WorkspaceCore {
    pub id: u32,
}

#[no_mangle]
pub extern "C" fn workspace_core_id(core: *const WorkspaceCore) -> u32 {
    if core.is_null() {
        0
    } else {
        unsafe { (*core).id }
    }
}
