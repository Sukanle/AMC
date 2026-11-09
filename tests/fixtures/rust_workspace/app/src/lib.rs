#[repr(C)]
pub struct WorkspaceApp {
    pub value: i32,
}

#[no_mangle]
pub extern "C" fn workspace_app_value(app: *const WorkspaceApp) -> i32 {
    if app.is_null() {
        0
    } else {
        unsafe { (*app).value }
    }
}
