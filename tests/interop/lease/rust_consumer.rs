// Rust consumer for the ABIX lease C ABI.
//
// The wrapper holds the Host function table, the registry handle and the
// registry-scoped token. `Drop` follows the required order: destroy the object,
// then release the lease.
use core::ffi::*;
use std::ffi::CString;

#[repr(C)]
struct AbixLeaseApi {
    abi_version: u32,
    struct_size: u32,
    host_context: *mut c_void,
    registry_create: Option<unsafe extern "C" fn(*mut c_void, *mut *mut c_void) -> i32>,
    registry_destroy: Option<unsafe extern "C" fn(*mut c_void, *mut c_void) -> i32>,
    acquire: Option<unsafe extern "C" fn(*mut c_void, *mut c_void, u64, *mut u64) -> i32>,
    release: Option<unsafe extern "C" fn(*mut c_void, *mut c_void, u64)>,
}

const ABIX_SUCCESS: i32 = 0;
const ABIX_ERROR_BUSY: i32 = 4;
const ABIX_ERROR_UNLOADING: i32 = 5;

extern "C" {
    fn lease_host_api() -> *const AbixLeaseApi;
    fn lease_host_registry() -> *mut c_void;
    fn lease_host_load(path: *const c_char) -> *mut c_void;
    fn lease_host_module(bridge: *mut c_void) -> u64;
    fn lease_host_create(bridge: *mut c_void) -> *mut c_void;
    fn lease_host_destroy(bridge: *mut c_void, object: *mut c_void) -> i32;
    fn lease_host_unregister(bridge: *mut c_void) -> i32;
    fn lease_host_finish(bridge: *mut c_void) -> i32;
    fn lease_host_registry_destroy() -> i32;
    fn lease_host_leases(bridge: *mut c_void) -> u32;

    fn lease_host_object_get(bridge: *mut c_void, object: *mut c_void) -> i32;
    fn lease_host_object_set(bridge: *mut c_void, object: *mut c_void, value: i32) -> i32;
}

struct OwnedSession {
    api: *const AbixLeaseApi,
    registry: *mut c_void,
    bridge: *mut c_void,
    object: *mut c_void,
    lease: u64,
}

impl OwnedSession {
    fn acquire(bridge: *mut c_void) -> Option<Self> {
        unsafe {
            let api = lease_host_api();
            if api.is_null() {
                return None;
            }
            let registry = lease_host_registry();
            if registry.is_null() {
                return None;
            }
            let acquire = (*api).acquire?;
            let mut lease = 0u64;
            let module = lease_host_module(bridge);
            if acquire((*api).host_context, registry, module, &mut lease) != ABIX_SUCCESS {
                return None;
            }

            let object = lease_host_create(bridge);
            if object.is_null() {
                // A failed create must release the lease immediately.
                if let Some(release) = (*api).release {
                    release((*api).host_context, registry, lease);
                }
                return None;
            }

            Some(Self { api, registry, bridge, object, lease })
        }
    }

    fn object(&self) -> *mut c_void { self.object }
}

impl Drop for OwnedSession {
    fn drop(&mut self) {
        unsafe {
            if !self.object.is_null() {
                let _ = lease_host_destroy(self.bridge, self.object);
                self.object = core::ptr::null_mut();
            }
            if self.lease != 0 {
                if let Some(release) = (*self.api).release {
                    release((*self.api).host_context, self.registry, self.lease);
                }
                self.lease = ABIX_LEASE_INVALID;
            }
        }
    }
}

const ABIX_LEASE_INVALID: u64 = 0;

unsafe fn check_api(api: *const AbixLeaseApi) {
    assert!(!api.is_null(), "lease_host_api");
    assert_eq!((*api).abi_version, 1, "abi_version");
    assert_eq!(
        (*api).struct_size as usize,
        core::mem::size_of::<AbixLeaseApi>(),
        "struct_size"
    );
    assert!((*api).registry_create.is_some(), "registry_create");
    assert!((*api).registry_destroy.is_some(), "registry_destroy");
    assert!((*api).acquire.is_some(), "acquire");
    assert!((*api).release.is_some(), "release");
}

fn main() {
    let module = std::env::args().nth(1).expect("module path");
    let module = CString::new(module).expect("module path NUL");

    unsafe {
        let api = lease_host_api();
        check_api(api);

        // acquire -> create -> use -> destroy -> release
        let bridge = lease_host_load(module.as_ptr());
        assert!(!bridge.is_null(), "lease_host_load");
        {
            let session = OwnedSession::acquire(bridge).expect("acquire + create");
            assert_eq!(lease_host_leases(bridge), 1, "module lease count");
            assert_eq!(lease_host_object_set(bridge, session.object(), 7), 0, "object set");
            assert_eq!(lease_host_object_get(bridge, session.object()), 7, "object get");

            // Draining: unregister reports busy, new acquires are rejected, but
            // the existing object can still be destroyed.
            assert_eq!(lease_host_unregister(bridge), ABIX_ERROR_BUSY, "unregister is busy");
            assert!(OwnedSession::acquire(bridge).is_none(), "acquire while draining must fail");
        }
        assert_eq!(lease_host_leases(bridge), 0, "lease released after drop");
        assert_eq!(lease_host_unregister(bridge), ABIX_SUCCESS, "unregister after release");
        assert_eq!(lease_host_finish(bridge), 0, "finish");

        // Two DSOs share one Host registry and one registry namespace.
        if let Some(second) = std::env::args().nth(2) {
            let module_b = CString::new(second).expect("second module path NUL");
            let bridge_a = lease_host_load(module.as_ptr());
            let bridge_b = lease_host_load(module_b.as_ptr());
            assert!(!bridge_a.is_null(), "DSO A load");
            assert!(!bridge_b.is_null(), "DSO B load");
            assert_ne!(
                lease_host_module(bridge_a),
                lease_host_module(bridge_b),
                "two DSOs get distinct module instances"
            );

            let session_a = OwnedSession::acquire(bridge_a).expect("DSO A acquire");
            let session_b = OwnedSession::acquire(bridge_b).expect("DSO B acquire");
            assert_eq!(lease_host_leases(bridge_a), 1, "DSO A lease count");
            assert_eq!(lease_host_leases(bridge_b), 1, "DSO B lease count");

            drop(session_a);
            assert_eq!(lease_host_leases(bridge_a), 0, "DSO A released");
            assert_eq!(lease_host_leases(bridge_b), 1, "releasing A must not release B");
            drop(session_b);

            assert_eq!(lease_host_unregister(bridge_a), ABIX_SUCCESS, "unregister A");
            assert_eq!(lease_host_unregister(bridge_b), ABIX_SUCCESS, "unregister B");
            assert_eq!(lease_host_finish(bridge_a), 0, "finish A");
            assert_eq!(lease_host_finish(bridge_b), 0, "finish B");
        }

        // Registry CLOSING / destroy / busy.
        let bridge = lease_host_load(module.as_ptr());
        assert!(!bridge.is_null(), "second lease_host_load");
        let session = OwnedSession::acquire(bridge).expect("second acquire");
        assert_eq!(lease_host_registry_destroy(), ABIX_ERROR_BUSY, "registry destroy is busy");
        assert!(OwnedSession::acquire(bridge).is_none(), "acquire after CLOSING must fail");
        drop(session);
        assert_eq!(lease_host_unregister(bridge), ABIX_SUCCESS, "second unregister");
        assert_eq!(lease_host_registry_destroy(), ABIX_SUCCESS, "registry destroy after release");
        assert_eq!(lease_host_finish(bridge), 0, "second finish");
    }

    println!("lease rust: OK");
}
