const std = @import("std");
const pg = @import("pg");

pub const Database = struct {
    pool: *pg.Pool,

    pub fn init(
        io: std.Io,
        allocator: std.mem.Allocator,
        connection_string: []const u8,
    ) !Database {
        const uri = try std.Uri.parse(connection_string);

        const pool = try pg.Pool.initUri(
            io,
            allocator,
            uri,
            .{
                .size = 5,
            },
        );

        errdefer pool.deinit();

        var db = Database{
            .pool = pool,
        };

        try db.ensureTableExists();

        return db;
    }

    pub fn deinit(self: *Database) void {
        self.pool.deinit();
    }

    // Создание таблиц и индексов
    fn ensureTableExists(self: *Database) !void {
        const create_cards =
            \\CREATE TABLE IF NOT EXISTS card_issuances (
            \\    uid VARCHAR(64) PRIMARY KEY,
            \\    issued BOOLEAN NOT NULL DEFAULT FALSE,
            \\    issued_at TIMESTAMPTZ
            \\);
        ;

        const create_events =
            \\CREATE TABLE IF NOT EXISTS rfid_events (
            \\    id BIGSERIAL PRIMARY KEY,
            \\    uid VARCHAR(64) NOT NULL,
            \\    event_type VARCHAR(32) NOT NULL,
            \\    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
            \\);
        ;

        const create_index_created =
            \\CREATE INDEX IF NOT EXISTS idx_rfid_events_created
            \\ON rfid_events(created_at);
        ;

        const create_index_uid =
            \\CREATE INDEX IF NOT EXISTS idx_rfid_events_uid
            \\ON rfid_events(uid);
        ;

        _ = try self.pool.exec(create_cards, .{});
        _ = try self.pool.exec(create_events, .{});
        _ = try self.pool.exec(create_index_created, .{});
        _ = try self.pool.exec(create_index_uid, .{});
    }

    // Проверка и регистрация RFID
    pub fn checkAndProcessCard(
        self: *Database,
        uid_hex: []const u8,
    ) !CardResult {
        const query =
            \\WITH changed AS (
            \\    INSERT INTO card_issuances (uid, issued, issued_at)
            \\    VALUES ($1, TRUE, NOW())
            \\    ON CONFLICT (uid)
            \\    DO UPDATE SET
            \\        issued = NOT card_issuances.issued,
            \\        issued_at = CASE
            \\            WHEN card_issuances.issued = FALSE THEN NOW()
            \\            ELSE NULL
            \\        END
            \\    RETURNING uid, issued
            \\),
            \\logged_event AS (
            \\    INSERT INTO rfid_events (uid, event_type)
            \\    SELECT
            \\        uid,
            \\        CASE
            \\            WHEN issued THEN 'board_taken'
            \\            ELSE 'board_returned'
            \\        END
            \\    FROM changed
            \\    RETURNING event_type
            \\)
            \\SELECT event_type FROM logged_event;
        ;

        var result = try self.pool.query(query, .{uid_hex});
        defer result.deinit();

        const row = (try result.next()) orelse {
            return error.EmptyQueryResult;
        };

        const event_type = try row.get([]const u8, 0);

        const taken = std.mem.eql(
            u8,
            event_type,
            "board_taken",
        );

        try result.drain();

        if (taken) {
            return .{
                .allowed = true,
                .status = "board_taken",
                .message = "Плата успешно взята",
            };
        }

        return .{
            .allowed = true,
            .status = "board_returned",
            .message = "Плата успешно сдана",
        };
    }

    // Общая статистика
    pub fn getStats(self: *Database) !DatabaseStats {
        const query =
            \\SELECT
            \\    (SELECT COUNT(*) FROM card_issuances),
            \\    (SELECT COUNT(*) FROM card_issuances WHERE issued = TRUE),
            \\    (SELECT COUNT(*) FROM card_issuances WHERE issued = FALSE),
            \\    (SELECT COUNT(*) FROM rfid_events),
            \\    (SELECT COUNT(*) FROM rfid_events
            \\        WHERE event_type = 'board_taken'),
            \\    (SELECT COUNT(*) FROM rfid_events
            \\        WHERE event_type = 'board_returned'),
            \\    (SELECT COUNT(*) FROM rfid_events
            \\        WHERE created_at >= date_trunc('day', NOW())),
            \\    COALESCE((
            \\        SELECT AVG(
            \\            EXTRACT(EPOCH FROM (next_time - created_at)) * 1000
            \\        )::bigint
            \\        FROM (
            \\            SELECT
            \\                event_type,
            \\                created_at,
            \\                LEAD(event_type) OVER (
            \\                    PARTITION BY uid ORDER BY created_at, id
            \\                ) AS next_event,
            \\                LEAD(created_at) OVER (
            \\                    PARTITION BY uid ORDER BY created_at, id
            \\                ) AS next_time
            \\            FROM rfid_events
            \\        ) AS events
            \\        WHERE event_type = 'board_taken'
            \\          AND next_event = 'board_returned'
            \\    ), 0);
        ;
        var result = try self.pool.query(query, .{});
        defer result.deinit();

        const row = (try result.next()) orelse {
            return error.EmptyStatsResult;
        };

        const stats = DatabaseStats{
            .total_cards = try row.get(i64, 0),
            .taken_cards = try row.get(i64, 1),
            .available_cards = try row.get(i64, 2),
            .total_events = try row.get(i64, 3),
            .total_takes = try row.get(i64, 4),
            .total_returns = try row.get(i64, 5),
            .events_today = try row.get(i64, 6),
            .avg_duration_ms = try row.get(i64, 7),
        };

        try result.drain();

        return stats;
    }

    // Последние 50 событий
    pub fn getHistory(self: *Database) !HistoryResult {
        const query =
            \\SELECT id, uid, event_type, created_at
            \\FROM rfid_events
            \\ORDER BY created_at DESC
            \\LIMIT 50;
        ;

        var result = try self.pool.query(query, .{});
        defer result.deinit();

        var history = HistoryResult{
            .entries = undefined,
            .len = 0,
        };

        while (try result.next()) |row| {
            if (history.len >= history.entries.len) {
                break;
            }

            const uid = try row.get([]const u8, 1);
            const event_type = try row.get([]const u8, 2);

            var entry: HistoryEntry = .{
                .id = try row.get(i64, 0),
                .uid = undefined,
                .uid_len = @min(uid.len, 64),
                .event_type = undefined,
                .event_type_len = @min(event_type.len, 32),
                .timestamp_us = try row.get(i64, 3),
            };

            @memcpy(
                entry.uid[0..entry.uid_len],
                uid[0..entry.uid_len],
            );

            @memcpy(
                entry.event_type[0..entry.event_type_len],
                event_type[0..entry.event_type_len],
            );

            history.entries[history.len] = entry;
            history.len += 1;
        }

        try result.drain();

        return history;
    }
};

pub const CardResult = struct {
    allowed: bool,
    status: []const u8,
    message: []const u8,
};

pub const DatabaseStats = struct {
    total_cards: i64,
    taken_cards: i64,
    available_cards: i64,
    total_events: i64,
    total_takes: i64,
    total_returns: i64,
    events_today: i64,
    avg_duration_ms: i64,
};

pub const HistoryEntry = struct {
    id: i64,
    uid: [64]u8,
    uid_len: usize,
    event_type: [32]u8,
    event_type_len: usize,
    timestamp_us: i64,
};

pub const HistoryResult = struct {
    entries: [50]HistoryEntry,
    len: usize,
};
