// Fixture for Rust ownership and error-boundary contract handoff.
//
// The functions are plain C ABI; the `[[owned]]`, `[[contract]]` and `[[error]]`
// declarations in the `.abic.toml` tell AMC how a consumer must treat the
// values. The Rust backend turns those declarations into an RAII wrapper and a
// typed `Result` wrapper.

use std::sync::atomic::{AtomicUsize, Ordering};

#[repr(C)]
pub struct RustOwnedBuffer {
    pub data: *mut u8,
    pub len: usize,
}

static LIVE: AtomicUsize = AtomicUsize::new(0);

#[no_mangle]
pub extern "C" fn rust_owned_create() -> *mut RustOwnedBuffer {
    let data: Box<[u8; 4]> = Box::new([1, 2, 3, 4]);
    let buffer = Box::new(RustOwnedBuffer {
        data: Box::into_raw(data) as *mut u8,
        len: 4,
    });
    LIVE.fetch_add(1, Ordering::SeqCst);
    Box::into_raw(buffer)
}

#[no_mangle]
pub extern "C" fn rust_owned_destroy(buffer: *mut RustOwnedBuffer) {
    if buffer.is_null() {
        return;
    }
    unsafe {
        let boxed = Box::from_raw(buffer);
        drop(Box::from_raw(boxed.data as *mut [u8; 4]));
    }
    LIVE.fetch_sub(1, Ordering::SeqCst);
}

#[no_mangle]
pub extern "C" fn rust_owned_live() -> usize {
    LIVE.load(Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn rust_div(a: i32, b: i32, out: *mut i32) -> i32 {
    if out.is_null() {
        return 2;
    }
    if b == 0 {
        return 1;
    }
    unsafe {
        *out = a / b;
    }
    0
}
