// Rust side of the Zig -> Rust lease handoff fixture.
//
// Zig creates the owned session (object + lease); this cdylib adopts the same
// object and lease, then follows the required order: destroy, release.
// It is a fixture, not part of AMC's compiler core.
use core::ffi::*;

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

extern "C" {
    fn lease_host_api() -> *const AbixLeaseApi;
    fn lease_host_destroy(bridge: *mut c_void, object: *mut c_void) -> i32;
    fn lease_host_object_get(bridge: *mut c_void, object: *mut c_void) -> i32;
    fn lease_host_live(bridge: *mut c_void) -> i32;
}

/// Adopt a Zig-created object and lease. Returns 0 on success.
///
/// The caller must not use `object` or `lease` after this returns, regardless
/// of the result: the function either destroys/releases them or reports a
/// contract error.
#[no_mangle]
pub unsafe extern "C" fn rust_takeover_adopt(
    bridge: *mut c_void,
    registry: *mut c_void,
    object: *mut c_void,
    lease: u64,
) -> i32 {
    if bridge.is_null() || registry.is_null() || object.is_null() || lease == 0 {
        return 1;
    }

    let api = lease_host_api();
    if api.is_null() {
        return 2;
    }
    if (*api).abi_version != 1 {
        return 3;
    }
    let release = match (*api).release {
        Some(release) => release,
        None => return 4,
    };

    // The object must still be alive and readable when Rust takes it over.
    if lease_host_object_get(bridge, object) < 0 {
        release((*api).host_context, registry, lease);
        return 5;
    }

    // The takeover owns the object from here: destroy it first.
    if lease_host_destroy(bridge, object) != 0 {
        // Do not leave the module pinned if the destroy entry failed.
        release((*api).host_context, registry, lease);
        return 6;
    }

    // Prove the destroy actually ran before the lease is released, so the test
    // cannot pass on a plain lease release with a leaked object.
    if lease_host_live(bridge) != 0 {
        release((*api).host_context, registry, lease);
        return 7;
    }

    release((*api).host_context, registry, lease);
    0
}
