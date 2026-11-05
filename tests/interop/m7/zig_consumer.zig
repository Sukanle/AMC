const std = @import("std");
const abi = @import("gen_zig/amc_generated.zig");

fn on_value(context: ?*anyopaque, value: c_int) callconv(.c) c_int {
    const total: *i32 = @ptrCast(@alignCast(context.?));
    total.* += value;
    return 0;
}

fn get_adapter(object: ?*anyopaque) callconv(.c) c_int {
    return abi.l3_session_get(object);
}

fn set_adapter(object: ?*anyopaque, value: c_int) callconv(.c) c_int {
    return abi.l3_session_set(object, value);
}

pub fn main() !void {
    const session = abi.l3_session_create(@ptrCast("demo"));
    if (session == null) return error.CreateFailed;
    var total: i32 = 0;
    if (abi.l3_session_subscribe(session, @ptrCast(@constCast(&on_value)), &total) != 0) return error.SubscribeFailed;
    if (abi.l3_session_emit(session, 7) != 0) return error.EmitFailed;
    if (total != 7) return error.CallbackNotCalled;

    const ok = abi.l3_session_div_result(session, 10, 2);
    switch (ok) {
        .ok => |value| if (value != 5) return error.BadValue,
        .err => return error.Unexpected,
    }
    const err = abi.l3_session_div_result(session, 10, 0);
    switch (err) {
        .ok => return error.Unexpected,
        .err => |code| if (abi.M7Errors.name(code)[0] != 'd') return error.BadError,
    }

    const methods = [_]abi.InterfaceMethodEntry{
        .{
            .signature_lo = abi.L3SessionOpsProxy.l3_session_get_signature_lo,
            .signature_hi = abi.L3SessionOpsProxy.l3_session_get_signature_hi,
            .entry = @intFromPtr(&get_adapter),
            .slot = abi.L3SessionOpsProxy.l3_session_get_slot,
            .flags = 0,
        },
        .{
            .signature_lo = abi.L3SessionOpsProxy.l3_session_set_signature_lo,
            .signature_hi = abi.L3SessionOpsProxy.l3_session_set_signature_hi,
            .entry = @intFromPtr(&set_adapter),
            .slot = abi.L3SessionOpsProxy.l3_session_set_slot,
            .flags = 0,
        },
    };
    const proxy = try abi.L3SessionOpsProxy.bind(
        abi.L3SessionOpsProxy.ID_LO,
        abi.L3SessionOpsProxy.ID_HI,
        abi.L3SessionOpsProxy.VERSION,
        session,
        methods[0..],
        null,
    );
    if (proxy.l3_session_set(9) != 0) return error.SetFailed;
    if (proxy.l3_session_get() != 9) return error.GetFailed;

    _ = abi.l3_session_destroy(session);
    std.debug.print("m7 zig: OK\n", .{});
}
