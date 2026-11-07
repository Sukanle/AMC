// Zig consumer for the ABIX lease C ABI.
//
// `OwnedSession.deinit()` follows the required order: destroy the object, then
// release the registry-scoped lease.
const std = @import("std");

const AbixLeaseApi = extern struct {
    abi_version: u32,
    struct_size: u32,
    host_context: ?*anyopaque,
    registry_create: *const fn (?*anyopaque, *?*anyopaque) callconv(.c) c_int,
    registry_destroy: *const fn (?*anyopaque, ?*anyopaque) callconv(.c) c_int,
    acquire: *const fn (?*anyopaque, ?*anyopaque, u64, *u64) callconv(.c) c_int,
    release: *const fn (?*anyopaque, ?*anyopaque, u64) callconv(.c) void,
};

const ABIX_SUCCESS: c_int = 0;
const ABIX_ERROR_BUSY: c_int = 4;
const ABIX_ERROR_UNLOADING: c_int = 5;

extern fn lease_host_api() *const AbixLeaseApi;
extern fn lease_host_registry() ?*anyopaque;
extern fn lease_host_load(path: [*c]const u8) ?*anyopaque;
extern fn lease_host_module(bridge: ?*anyopaque) u64;
extern fn lease_host_create(bridge: ?*anyopaque) ?*anyopaque;
extern fn lease_host_destroy(bridge: ?*anyopaque, object: ?*anyopaque) c_int;
extern fn lease_host_unregister(bridge: ?*anyopaque) c_int;
extern fn lease_host_finish(bridge: ?*anyopaque) c_int;
extern fn lease_host_registry_destroy() c_int;
extern fn lease_host_leases(bridge: ?*anyopaque) u32;

extern fn lease_host_object_get(bridge: ?*anyopaque, object: ?*anyopaque) c_int;
extern fn lease_host_object_set(bridge: ?*anyopaque, object: ?*anyopaque, value: c_int) c_int;
extern fn lease_host_live(bridge: ?*anyopaque) c_int;

// Rust side of the Zig -> Rust handoff. It destroys the object and releases the
// lease, then returns 0; Zig must not use the session afterwards.
extern fn rust_takeover_adopt(
    bridge: ?*anyopaque,
    registry: ?*anyopaque,
    object: ?*anyopaque,
    lease: u64,
) c_int;

const OwnedSession = struct {
    api: *const AbixLeaseApi,
    registry: ?*anyopaque,
    bridge: ?*anyopaque,
    object: ?*anyopaque,
    lease: u64,

    fn acquire(bridge: ?*anyopaque) !OwnedSession {
        const api = lease_host_api();
        if (api.abi_version != 1) return error.BadApiVersion;
        if (api.struct_size != @sizeOf(AbixLeaseApi)) return error.BadStructSize;
        const registry = lease_host_registry() orelse return error.RegistryUnavailable;
        const module = lease_host_module(bridge);
        var lease: u64 = 0;
        if (api.acquire(api.host_context, registry, module, &lease) != ABIX_SUCCESS) {
            return error.AcquireFailed;
        }
        const object = lease_host_create(bridge) orelse {
            api.release(api.host_context, registry, lease);
            return error.CreateFailed;
        };
        return .{
            .api = api,
            .registry = registry,
            .bridge = bridge,
            .object = object,
            .lease = lease,
        };
    }

    fn deinit(self: *OwnedSession) void {
        if (self.object) |object| {
            _ = lease_host_destroy(self.bridge, object);
            self.object = null;
        }
        if (self.lease != 0) {
            self.api.release(self.api.host_context, self.registry, self.lease);
            self.lease = 0;
        }
    }
};

const module_path = "LEASE_MODULE_PATH";

pub fn main() !void {
    const bridge = lease_host_load(module_path) orelse return error.LoadFailed;

    {
        var session = try OwnedSession.acquire(bridge);
        if (lease_host_leases(bridge) != 1) return error.BadLeaseCount;
        if (lease_host_object_set(bridge, session.object, 7) != 0) return error.SetFailed;
        if (lease_host_object_get(bridge, session.object) != 7) return error.GetFailed;

        if (lease_host_unregister(bridge) != ABIX_ERROR_BUSY) return error.UnregisterShouldBeBusy;
        if (OwnedSession.acquire(bridge)) |_| {
            return error.AcquireWhileDraining;
        } else |err| switch (err) {
            error.AcquireFailed => {},
            else => return err,
        }

        // Zig created the session; Rust takes it over and is responsible for
        // destroy + release. Zig must not deinit it afterwards.
        const takeover = rust_takeover_adopt(bridge, session.registry, session.object, session.lease);
        if (takeover != 0) return error.RustTakeoverFailed;
        session.object = null;
        session.lease = 0;
        // Rust must have destroyed the object, not only released the lease.
        if (lease_host_live(bridge) != 0) return error.ObjectNotDestroyed;
    }

    if (lease_host_leases(bridge) != 0) return error.LeaseNotReleased;
    if (lease_host_unregister(bridge) != ABIX_SUCCESS) return error.UnregisterFailed;
    if (lease_host_finish(bridge) != 0) return error.FinishFailed;

    // Registry CLOSING / destroy / busy.
    const bridge2 = lease_host_load(module_path) orelse return error.SecondLoadFailed;
    var session2 = try OwnedSession.acquire(bridge2);
    if (lease_host_registry_destroy() != ABIX_ERROR_BUSY) return error.RegistryShouldBeBusy;
    if (OwnedSession.acquire(bridge2)) |_| {
        return error.AcquireAfterClosing;
    } else |err| switch (err) {
        error.AcquireFailed => {},
        else => return err,
    }
    session2.deinit();
    if (lease_host_unregister(bridge2) != ABIX_SUCCESS) return error.SecondUnregisterFailed;
    if (lease_host_registry_destroy() != ABIX_SUCCESS) return error.RegistryDestroyFailed;
    if (lease_host_finish(bridge2) != 0) return error.SecondFinishFailed;

    std.debug.print("lease zig: OK\n", .{});
}
