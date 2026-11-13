//! # Database
//!
//! Данный модуль отвечает за взаимодействие приложения RFID Card Manager
//! с реляционной базой данных PostgreSQL.
//!
//! Реализует:
//! - Инициализацию пула подключений к PostgreSQL.
//! - Автоматическое создание необходимых таблиц и индексов.
//! - Регистрацию RFID-карт и изменение их состояния.
//! - Сохранение истории операций с картами.
//! - Получение общей статистики системы.
//! - Получение последних событий из истории.
//!
//! ## Структура базы данных
//!
//! Модуль использует две основные таблицы:
//!
//! - card_issuances — хранение информации о выданных картах
//!   и их текущем состоянии.
//! - rfid_events — журнал событий получения и возврата карт.
//!
//! ## Зависимости
//!
//! - std — стандартная библиотека Zig.
//! - pg — библиотека для взаимодействия с PostgreSQL.
//!
//! ## Управление ресурсами
//!
//! Для выполнения запросов используется пул подключений pg.Pool.
//! Пул создаётся при инициализации Database и освобождается
//! при вызове Database.deinit().
const std = @import("std");
const pg = @import("pg");

/// Основная структура для взаимодействия с PostgreSQL.
///
/// Содержит пул подключений к базе данных и предоставляет
/// методы для выполнения запросов, связанных с RFID-картами.
///
/// Поля:
///     pool: Указатель на пул подключений PostgreSQL.
///
/// Memory:
///     Владеет пулом подключений, созданным в Database.init().
///     Освобождение пула выполняется через Database.deinit().
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

    /// Освобождает ресурсы подключения к PostgreSQL.
    ///
    /// Закрывает пул подключений и освобождает связанные с ним ресурсы.
    ///
    /// Parameters:
    ///     self: Указатель на экземпляр Database.
    ///
    /// Returns:
    ///     void.
    ///
    /// Important:
    ///     После вызова данного метода экземпляр Database
    ///     больше не должен использоваться.
    pub fn deinit(self: *Database) void {
        self.pool.deinit();
    }

    /// Проверяет наличие необходимых таблиц и индексов.
    ///
    /// Выполняет SQL-команды CREATE TABLE IF NOT EXISTS
    /// и CREATE INDEX IF NOT EXISTS.
    ///
    /// Создаваемые таблицы:
    ///
    /// card_issuances:
    ///     uid — уникальный идентификатор RFID-карты.
    ///     issued — текущее состояние карты.
    ///     issued_at — время последней выдачи карты.
    ///
    /// rfid_events:
    ///     id — уникальный идентификатор события.
    ///     uid — UID RFID-карты.
    ///     event_type — тип события.
    ///     created_at — время регистрации события.
    ///
    /// Создаваемые индексы:
    ///     idx_rfid_events_created — индекс по времени события.
    ///     idx_rfid_events_uid — индекс по UID карты.
    ///
    /// Parameters:
    ///     self: Указатель на экземпляр Database.
    ///
    /// Errors:
    ///     Возвращает ошибку при невозможности выполнить
    ///     одну из SQL-команд.
    ///
    /// Important:
    ///     Метод вызывается автоматически при инициализации Database.
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

    /// Проверяет RFID-карту и изменяет её состояние.
    ///
    /// Выполняет атомарную операцию переключения состояния карты
    /// с одновременной регистрацией соответствующего события
    /// в таблице rfid_events.
    ///
    /// Алгоритм работы:
    /// 1. Если карта отсутствует в card_issuances,
    ///    создаётся новая запись со статусом issued = TRUE.
    /// 2. Если карта уже существует, её состояние инвертируется.
    /// 3. При выдаче карты устанавливается issued_at = NOW().
    /// 4. При возврате карты значение issued_at сбрасывается в NULL.
    /// 5. В rfid_events добавляется соответствующее событие.
    /// 6. Возвращается результат выполненной операции.
    ///
    /// Типы событий:
    ///     board_taken — карта выдана.
    ///     board_returned — карта возвращена.
    ///
    /// Parameters:
    ///     self: Указатель на экземпляр Database.
    ///     uid_hex: UID RFID-карты в шестнадцатеричном формате.
    ///
    /// Returns:
    ///     CardResult, содержащий:
    ///     - allowed — результат разрешения операции.
    ///     - status — тип выполненной операции.
    ///     - message — сообщение для пользователя.
    ///
    /// Errors:
    ///     Ошибки выполнения SQL-запроса.
    ///     error.EmptyQueryResult — запрос не вернул результат.
    ///
    /// Important:
    ///     Изменение состояния карты и добавление события
    ///     выполняются в рамках одного SQL-оператора.
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

    /// Получает общую статистику работы системы.
    ///
    /// Выполняет SQL-запрос для получения агрегированных
    /// показателей по зарегистрированным картам и событиям.
    ///
    /// Возвращаемые показатели:
    ///     total_cards — общее количество зарегистрированных карт.
    ///     taken_cards — количество выданных карт.
    ///     available_cards — количество возвращённых карт.
    ///     total_events — общее количество событий.
    ///     total_takes — количество выдач карт.
    ///     total_returns — количество возвратов карт.
    ///     events_today — количество событий за текущий день.
    ///     avg_duration_ms — средняя продолжительность использования карты.
    ///
    /// Средняя продолжительность рассчитывается по времени
    /// между событием board_taken и следующим board_returned
    /// для той же карты.
    ///
    /// Parameters:
    ///     self: Указатель на экземпляр Database.
    ///
    /// Returns:
    ///     DatabaseStats со статистическими показателями.
    ///
    /// Errors:
    ///     Ошибка выполнения SQL-запроса.
    ///     error.EmptyStatsResult — запрос не вернул строку статистики.
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

    /// Получает последние события работы с RFID-картами.
    ///
    /// Выполняет выборку последних 50 записей из rfid_events,
    /// сортируя их по времени создания в порядке убывания.
    ///
    /// Для каждой записи извлекаются:
    /// - Идентификатор события.
    /// - UID RFID-карты.
    /// - Тип события.
    /// - Время создания события.
    ///
    /// Полученные строки преобразуются в HistoryEntry
    /// и сохраняются в HistoryResult.
    ///
    /// Parameters:
    ///     self: Указатель на экземпляр Database.
    ///
    /// Returns:
    ///     HistoryResult, содержащий массив событий и количество
    ///     фактически полученных записей.
    ///
    /// Memory:
    ///     Результат хранится в массиве фиксированного размера
    ///     на 50 элементов.
    ///     UID и event_type копируются в собственные массивы
    ///     фиксированного размера.
    ///
    /// Errors:
    ///     Ошибки выполнения SQL-запроса.
    ///     Ошибки извлечения и преобразования полей PostgreSQL.
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

/// Результат обработки RFID-карты.
///
/// Содержит информацию о выполненной операции,
/// которая впоследствии может быть передана HTTP-клиенту.
///
/// Поля:
///     allowed: Признак разрешения операции.
///     status: Строковый идентификатор результата.
///     message: Текстовое сообщение для пользователя.
pub const CardResult = struct {
    /// Признак успешного выполнения операции.
    allowed: bool,

    /// Идентификатор выполненного события.
    /// Возможные значения: board_taken, board_returned.
    status: []const u8,

    /// Текстовое описание результата операции.
    message: []const u8,
};

/// Структура общей статистики системы.
///
/// Содержит агрегированные показатели работы RFID Card Manager.
/// Все числовые значения получаются из PostgreSQL.
///
/// Поля:
///     total_cards: Общее количество зарегистрированных карт.
///     taken_cards: Количество выданных карт.
///     available_cards: Количество возвращённых карт.
///     total_events: Общее количество событий.
///     total_takes: Количество операций выдачи.
///     total_returns: Количество операций возврата.
///     events_today: Количество событий за текущий день.
///     avg_duration_ms: Средняя продолжительность использования карты в мс.
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

/// Запись об отдельном событии работы с RFID-картой.
///
/// Содержит информацию об идентификаторе события,
/// использованной карте, типе операции и времени её выполнения.
///
/// Поля:
///     id: Уникальный идентификатор события PostgreSQL.
///     uid: Массив байтов, содержащий UID карты в текстовом виде.
///     uid_len: Фактическая длина UID.
///     event_type: Массив байтов с типом события.
///     event_type_len: Фактическая длина типа события.
///     timestamp_us: Время события в микросекундах Unix.
pub const HistoryEntry = struct {
    id: i64,
    uid: [64]u8,
    uid_len: usize,
    event_type: [32]u8,
    event_type_len: usize,
    timestamp_us: i64,
};

/// Результат получения истории событий.
///
/// Содержит фиксированный массив последних событий
/// и количество фактически заполненных элементов.
///
/// Поля:
///     entries: Массив записей истории размером 50 элементов.
///     len: Количество действительных записей в массиве.
pub const HistoryResult = struct {
    entries: [50]HistoryEntry,
    len: usize,
};
