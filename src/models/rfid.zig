const std = @import("std");

pub const RFIDCardRequest = struct {
    size: usize,
    bytes: []const u8,
};
