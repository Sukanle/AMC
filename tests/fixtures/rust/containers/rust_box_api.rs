// Explicit `Box<T>` ownership fixture: the source functions keep ordinary Rust
// signatures, while `[[rust.expose]]` + `[[adapt]]` turn `Box<BoxWidget>` into
// an owned `*mut BoxWidget` handle. `[[owned]]` and `[[contract]]` name the
// create/destroy pair; AMC does not synthesize them.

use std::sync::atomic::{AtomicUsize, Ordering};

static LIVE: AtomicUsize = AtomicUsize::new(0);

#[repr(C)]
pub struct BoxWidget {
    pub value: i32,
}

impl Drop for BoxWidget {
    fn drop(&mut self) {
        LIVE.fetch_sub(1, Ordering::SeqCst);
    }
}

pub fn box_widget_make(value: i32) -> Box<BoxWidget> {
    LIVE.fetch_add(1, Ordering::SeqCst);
    Box::new(BoxWidget { value })
}

pub fn box_widget_value(widget: Box<BoxWidget>) -> i32 {
    widget.value
}

#[no_mangle]
pub extern "C" fn box_widget_destroy(ptr: *mut BoxWidget) {
    if ptr.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(ptr));
    }
}

#[no_mangle]
pub extern "C" fn box_widget_live() -> usize {
    LIVE.load(Ordering::SeqCst)
}
