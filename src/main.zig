const std = @import("std");
const server = @import("server.zig");
const Database = @import("database/db.zig").Database;

pub fn main(init: std.process.Init) !void {
    const allocator = init.gpa;
    const io = init.io;

    const db_url = init.environ_map.get("DATABASE_URL") orelse {
        std.debug.print("DATABASE_URL is not set!\n", .{});
        return error.MissingDatabaseUrl;
    };

    std.debug.print("Connecting to remote database...\n", .{});

    var db = try Database.init(
        io,
        allocator,
        db_url,
    );
    defer db.deinit();

    std.debug.print("Database connected successfully!\n", .{});

    try server.start(io, allocator, 8080, &db);
}
