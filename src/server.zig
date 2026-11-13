const std = @import("std");
const net = std.Io.net;

const RFID = @import("models/rfid.zig");
const Database = @import("database/db.zig").Database;

pub fn start(
    io: std.Io,
    allocator: std.mem.Allocator,
    port: u16,
    db: *Database,
) !void {
    const address = try net.IpAddress.parse("0.0.0.0", port);

    var net_server = try address.listen(io, .{
        .reuse_address = true,
    });
    defer net_server.deinit(io);

    std.debug.print(
        "Server is running at http://127.0.0.1:{d}\n",
        .{port},
    );

    try mainloop(io, allocator, &net_server, db);
}

fn mainloop(
    io: std.Io,
    allocator: std.mem.Allocator,
    net_server: *net.Server,
    db: *Database,
) !void {
    while (true) {
        const conn = net_server.accept(io) catch |err| {
            std.debug.print("Accept error: {}\n", .{err});
            continue;
        };

        handleConnection(io, allocator, conn, db) catch |err| {
            std.debug.print("Connection error: {}\n", .{err});
        };
    }
}

fn handleConnection(
    io: std.Io,
    allocator: std.mem.Allocator,
    conn: net.Stream,
    db: *Database,
) !void {
    defer conn.close(io);

    var read_buf: [8192]u8 = undefined;
    var reader = conn.reader(io, &read_buf);

    var write_buf: [4096]u8 = undefined;
    var writer = conn.writer(io, &write_buf);

    // Читаем первую строку HTTP-запроса
    const request_line = try reader.interface.takeDelimiterInclusive('\n');
    const request = std.mem.trimEnd(u8, request_line, "\r\n");
    const is_post = std.mem.startsWith(u8, request, "POST ");
    const has_endpoint = std.mem.indexOf(u8, request, "/api/checkUID") != null;

    // Читаем HTTP-заголовки
    var content_len: usize = 0;
    var has_content_length = false;

    while (true) {
        const header_line = reader.interface.takeDelimiterInclusive('\n') catch |err| {
            std.debug.print("Header read error: {}\n", .{err});
            return;
        };

        const header = std.mem.trim(u8, header_line, " \t\r\n");

        if (header.len == 0) {
            break;
        }

        std.debug.print("HTTP Header: [{s}]\n", .{header});

        const colon = std.mem.indexOfScalar(u8, header, ':') orelse continue;

        const name = std.mem.trim(u8, header[0..colon], " \t");
        const value = std.mem.trim(u8, header[colon + 1 ..], " \t");

        if (std.ascii.eqlIgnoreCase(name, "Content-Length")) {
            content_len = std.fmt.parseInt(usize, value, 10) catch {
                std.debug.print("Invalid Content-Length: {s}\n", .{value});
                return;
            };

            has_content_length = true;
        }
    }

    std.debug.print(
        "Content-Length parsed: {d}, found: {}\n",
        .{ content_len, has_content_length },
    );
    std.debug.print(
        "\nHTTP request: {s}\nContent-Length: {d}\n",
        .{ request, content_len },
    );

    // Если запрашивают корень или файлы статики
    if (std.mem.startsWith(u8, request, "GET / ")) {
        // Читаем public/index.html с диска
        const file_content = std.fs.cwd().readFileAlloc(allocator, "public/index.html", 1024 * 1024) catch {
            try sendResponse(&writer.interface, 404, "{\"error\":\"Index not found\"}");
            return;
        };
        defer allocator.free(file_content);

        try sendCustomResponse(&writer.interface, 200, "text/html; charset=utf-8", file_content);
        return;
    }

    // Раздача статики из ./public
    if (std.mem.startsWith(u8, request, "GET ") and !std.mem.startsWith(u8, request, "GET /api/")) {
        // Извлекаем путь между "GET " и " HTTP/1.1"
        const after_method = request[4..];
        const space_idx = std.mem.indexOfScalar(u8, after_method, ' ') orelse after_method.len;
        var path = after_method[0..space_idx];

        if (std.mem.indexOfScalar(u8, path, '?')) |q_idx| {
            path = path[0..q_idx];
        }

        if (std.mem.indexOf(u8, path, "..") != null) {
            try sendResponse(&writer.interface, 403, "{\"error\":\"Forbidden\"}");
            return;
        }

        const file_path = if (std.mem.eql(u8, path, "/"))
            "public/index.html"
        else
            try std.fmt.allocPrint(allocator, "public{s}", .{path});
        defer if (!std.mem.eql(u8, path, "/")) allocator.free(file_path);

        const content = std.fs.cwd().readFileAlloc(allocator, file_path, 5 * 1024 * 1024) catch {
            try sendResponse(&writer.interface, 404, "{\"error\":\"File not found\"}");
            return;
        };
        defer allocator.free(content);

        const content_type = if (std.mem.endsWith(u8, file_path, ".html"))
            "text/html; charset=utf-8"
        else if (std.mem.endsWith(u8, file_path, ".css"))
            "text/css; charset=utf-8"
        else if (std.mem.endsWith(u8, file_path, ".js"))
            "application/javascript; charset=utf-8"
        else
            "application/octet-stream";

        try writer.interface.print(
            "HTTP/1.1 200 OK\r\n" ++
                "Content-Type: {s}\r\n" ++
                "Content-Length: {d}\r\n" ++
                "Connection: close\r\n\r\n",
            .{ content_type, content.len },
        );
        try writer.interface.writeAll(content);
        try writer.interface.flush();
        return;
    }

    // GET /api/stats
    if (std.mem.startsWith(u8, request, "GET /api/stats ")) {
        try sendStatsResponse(&writer.interface, db);
        return;
    }

    // GET /api/history
    const is_get_history =
        std.mem.startsWith(u8, request, "GET /api/history ") or
        std.mem.startsWith(u8, request, "GET /api/history?");

    if (is_get_history) {
        try sendHistoryResponse(&writer.interface, db);
        return;
    }

    // Обработка CORS preflight
    if (!has_content_length) {
        try sendResponse(
            &writer.interface,
            411,
            "{\"error\":\"Content-Length required\"}",
        );
        return;
    }

    if (content_len == 0 or content_len > 4096) {
        try sendResponse(
            &writer.interface,
            400,
            "{\"error\":\"Invalid Content-Length\"}",
        );
        return;
    }
    // Проверяем endpoint
    if (!is_post or !has_endpoint) {
        try sendResponse(
            &writer.interface,
            404,
            "{\"error\":\"Not Found\"}",
        );
        return;
    }

    // Проверяем размер тела запроса
    if (content_len == 0 or content_len > 4096) {
        std.debug.print("400: Invalid Content-Length = {d}\n", .{content_len});

        try sendResponse(
            &writer.interface,
            400,
            "{\"error\":\"Invalid Content-Length\"}",
        );
        return;
    }

    // Читаем тело запроса
    var body_buf: [4096]u8 = undefined;
    const body = body_buf[0..content_len];

    reader.interface.readSliceAll(body) catch |err| {
        std.debug.print("400: Failed to read body: {}\n", .{err});

        try sendResponse(
            &writer.interface,
            400,
            "{\"error\":\"Failed to read body\"}",
        );
        return;
    };

    const clean_body = std.mem.trim(u8, body, " \t\r\n");

    std.debug.print(
        "Received body ({d} bytes): {s}\n",
        .{ clean_body.len, clean_body },
    );

    // Парсим JSON
    const parsed = std.json.parseFromSlice(
        RFID.RFIDCardRequest,
        allocator,
        clean_body,
        .{ .allocate = .alloc_always },
    ) catch |err| {
        std.debug.print("JSON parse error: {}\n", .{err});

        try sendResponse(
            &writer.interface,
            400,
            "{\"error\":\"Invalid JSON format\"}",
        );
        return;
    };
    defer parsed.deinit();

    const card = parsed.value;

    // Проверяем длину UID
    if (card.size == 0 or card.size > card.bytes.len) {
        try sendResponse(
            &writer.interface,
            400,
            "{\"error\":\"Invalid RFID UID size\"}",
        );
        return;
    }

    // Форматируем UID в HEX
    var hex_buf: [32]u8 = undefined;
    var hex_len: usize = 0;

    const hex_digits = "0123456789ABCDEF";

    for (card.bytes[0..card.size]) |byte| {
        hex_buf[hex_len] = hex_digits[@intCast(byte >> 4)];
        hex_buf[hex_len + 1] = hex_digits[@intCast(byte & 0x0F)];

        hex_len += 2;
    }

    const uid_hex = hex_buf[0..hex_len];

    std.debug.print(
        "Received RFID UID: {s} (size: {d})\n",
        .{ uid_hex, card.size },
    );

    // Проверяем карту через PostgreSQL
    const check_res = db.checkAndProcessCard(uid_hex) catch |err| {
        std.debug.print("Database error: {}\n", .{err});

        try sendResponse(
            &writer.interface,
            500,
            "{\"error\":\"Database processing failed\"}",
        );
        return;
    };

    // Формируем JSON-ответ
    var resp_buf: [256]u8 = undefined;

    const resp_json = try std.fmt.bufPrint(
        &resp_buf,
        "{{\"allowed\":{s},\"status\":\"{s}\",\"message\":\"{s}\"}}",
        .{
            if (check_res.allowed) "true" else "false",
            check_res.status,
            check_res.message,
        },
    );

    const http_status: u16 = if (check_res.allowed)
        200
    else
        409;

    try sendResponse(
        &writer.interface,
        http_status,
        resp_json,
    );
}

fn sendResponse(
    writer: *std.Io.Writer,
    status: u16,
    body: []const u8,
) !void {
    const status_text = switch (status) {
        200 => "OK",
        400 => "Bad Request",
        404 => "Not Found",
        409 => "Conflict",
        411 => "Length Required",
        500 => "Internal Server Error",
        else => "Internal Server Error",
    };

    try writer.print(
        "HTTP/1.1 {d} {s}\r\n" ++
            "Content-Type: application/json; charset=utf-8\r\n" ++
            "Content-Length: {d}\r\n" ++
            "Access-Control-Allow-Origin: *\r\n" ++
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n" ++
            "Access-Control-Allow-Headers: Content-Type\r\n" ++
            "Connection: close\r\n" ++
            "\r\n" ++
            "{s}",
        .{
            status,
            status_text,
            body.len,
            body,
        },
    );

    try writer.flush();
}

fn sendStatsResponse(
    writer: *std.Io.Writer,
    db: *Database,
) !void {
    const stats = db.getStats() catch |err| {
        std.debug.print("Stats database error: {}\n", .{err});

        try sendResponse(
            writer,
            500,
            "{\"error\":\"Failed to get statistics\"}",
        );
        return;
    };

    var buffer: [512]u8 = undefined;

    const json = try std.fmt.bufPrint(
        &buffer,
        "{{\"total_cards\":{d}," ++
            "\"taken_cards\":{d}," ++
            "\"available_cards\":{d}," ++
            "\"total_events\":{d}," ++
            "\"total_takes\":{d}," ++
            "\"total_returns\":{d}," ++
            "\"events_today\":{d}," ++
            "\"avg_duration_ms\":{d}}}",
        .{
            stats.total_cards,
            stats.taken_cards,
            stats.available_cards,
            stats.total_events,
            stats.total_takes,
            stats.total_returns,
            stats.events_today,
            stats.avg_duration_ms,
        },
    );

    try sendResponse(writer, 200, json);
}

fn sendHistoryResponse(
    writer: *std.Io.Writer,
    db: *Database,
) !void {
    const history = db.getHistory() catch |err| {
        std.debug.print("History database error: {}\n", .{err});

        try sendResponse(
            writer,
            500,
            "{\"error\":\"Failed to get history\"}",
        );
        return;
    };

    var buffer: [12288]u8 = undefined;
    var offset: usize = 0;

    const prefix = "{\"items\":[";

    @memcpy(buffer[0..prefix.len], prefix);
    offset += prefix.len;

    for (history.entries[0..history.len], 0..) |entry, index| {
        const item = try std.fmt.bufPrint(
            buffer[offset..],
            "{s}{{\"id\":{d},\"uid\":\"{s}\",\"event_type\":\"{s}\",\"timestamp\":{d}}}",
            .{
                if (index == 0) "" else ",",
                entry.id,
                entry.uid[0..entry.uid_len],
                entry.event_type[0..entry.event_type_len],
                @divTrunc(entry.timestamp_us, 1000),
            },
        );

        offset += item.len;
    }

    const suffix = "]}";

    @memcpy(
        buffer[offset .. offset + suffix.len],
        suffix,
    );

    offset += suffix.len;

    try sendResponse(
        writer,
        200,
        buffer[0..offset],
    );
}

fn sendCustomResponse(
    writer: *std.Io.Writer,
    status: u16,
    content_type: []const u8,
    body: []const u8,
) !void {
    const status_text = switch (status) {
        200 => "OK",
        404 => "Not Found",
        else => "Internal Server Error",
    };

    try writer.print(
        "HTTP/1.1 {d} {s}\r\n" ++
            "Content-Type: {s}\r\n" ++
            "Content-Length: {d}\r\n" ++
            "Connection: close\r\n" ++
            "\r\n" ++
            "{s}",
        .{
            status,
            status_text,
            content_type,
            body.len,
            body,
        },
    );

    try writer.flush();
}
