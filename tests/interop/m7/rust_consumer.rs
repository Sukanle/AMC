#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;
use core::ffi::*;
use std::sync::atomic::{AtomicUsize, Ordering};

static RELEASED: AtomicUsize = AtomicUsize::new(0);

unsafe extern "C" fn release_lease(_raw: *mut c_void) {
    RELEASED.fetch_add(1, Ordering::SeqCst);
}

extern "C" fn on_value(context: *mut c_void, value: c_int) -> c_int {
    unsafe {
        let total = context as *mut i32;
        *total += value;
    }
    0
}

unsafe extern "C" fn get_adapter(object: *mut c_void) -> c_int {
    unsafe { l3_session_get(object as *const c_void) }
}

unsafe extern "C" fn set_adapter(object: *mut c_void, value: c_int) -> c_int {
    unsafe { l3_session_set(object, value) }
}

fn main() {
    unsafe {
        let session = OwnedL3Session::create(b"demo\0".as_ptr() as *const c_char).expect("create");
        let mut total: i32 = 0;
        assert_eq!(
            l3_session_subscribe(
                session.as_ptr(),
                on_value as *const () as *mut c_void,
                &mut total as *mut i32 as *mut c_void,
            ),
            0
        );
        assert_eq!(l3_session_emit(session.as_ptr(), 7), 0);
        assert_eq!(total, 7, "callback invoked");

        assert_eq!(l3_session_div_result(session.as_ptr(), 10, 2), Ok(5));
        let err = l3_session_div_result(session.as_ptr(), 10, 0).expect_err("div zero");
        assert_eq!(err.0, 1);
        assert_eq!(err.name(), "div_zero");

        let methods = [
            InterfaceMethodEntry {
                signature_lo: L3SessionOpsProxy::L3_SESSION_GET_SIGNATURE.0,
                signature_hi: L3SessionOpsProxy::L3_SESSION_GET_SIGNATURE.1,
                entry: get_adapter as usize,
                slot: L3SessionOpsProxy::L3_SESSION_GET_SLOT,
                flags: 0,
            },
            InterfaceMethodEntry {
                signature_lo: L3SessionOpsProxy::L3_SESSION_SET_SIGNATURE.0,
                signature_hi: L3SessionOpsProxy::L3_SESSION_SET_SIGNATURE.1,
                entry: set_adapter as usize,
                slot: L3SessionOpsProxy::L3_SESSION_SET_SLOT,
                flags: 0,
            },
        ];
        let proxy = L3SessionOpsProxy::bind(
            (L3SessionOpsProxy::ID_LO, L3SessionOpsProxy::ID_HI),
            L3SessionOpsProxy::VERSION,
            session.as_ptr() as *mut c_void,
            &methods,
            None,
        )
        .expect("bind interface");
        assert_eq!(proxy.l3_session_get(), 0);
        assert_eq!(proxy.l3_session_set(9), 0);
        assert_eq!(proxy.l3_session_get(), 9);

        let wrong = L3SessionOpsProxy::bind(
            (L3SessionOpsProxy::ID_LO, L3SessionOpsProxy::ID_HI),
            L3SessionOpsProxy::VERSION + 1,
            session.as_ptr() as *mut c_void,
            &methods,
            None,
        );
        assert_eq!(wrong.err(), Some(BindError::VersionMismatch));

        {
            let lease = InterfaceLease::from_raw(core::ptr::null_mut(), release_lease);
            let leased = L3SessionOpsProxy::bind(
                (L3SessionOpsProxy::ID_LO, L3SessionOpsProxy::ID_HI),
                L3SessionOpsProxy::VERSION,
                session.as_ptr() as *mut c_void,
                &methods,
                Some(lease),
            )
            .expect("leased bind");
            assert!(leased.has_lease());
        }
        assert_eq!(RELEASED.load(Ordering::SeqCst), 1, "lease released on drop");
    }
    println!("m7 rust: OK");
}
