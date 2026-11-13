//! # Main
//!
//! Данный модуль является точкой входа в приложение RFID Card Manager.
//!
//! Отвечает за:
//! - Получение параметров окружения.
//! - Инициализацию аллокатора памяти и интерфейса ввода-вывода.
//! - Установление соединения с PostgreSQL.
//! - Инициализацию и запуск HTTP-сервера.
//!
//! ## Зависимости
//! - `server.zig` — HTTP-сервер приложения.
//! - `database/db.zig` — модуль взаимодействия с PostgreSQL.

const std = @import("std");
const server = @import("server.zig");
const Database = @import("database/db.zig").Database;

/// Главная функция приложения.
///
/// Выполняет первоначальную настройку приложения, устанавливает
/// соединение с PostgreSQL и запускает HTTP-сервер.
///
/// Аллокатор памяти и интерфейс ввода-вывода извлекаются
/// из переданной структуры `std.process.Init`.
///
/// Последовательность выполнения:
/// 1. Получение аллокатора памяти и интерфейса ввода-вывода.
/// 2. Чтение переменной окружения `DATABASE_URL`.
/// 3. Инициализация подключения к PostgreSQL.
/// 4. Запуск HTTP-сервера на порту 8080.
///
/// Parameters:
///     init: Структура инициализации процесса, содержащая
///           параметры окружения, аллокатор памяти и интерфейс IO.
///
/// Returns:
///     void — функция не возвращает пользовательских данных.
///
/// Errors:
///     error.MissingDatabaseUrl — переменная DATABASE_URL отсутствует
///                               в окружении процесса.
///     Ошибки Database.init — при невозможности подключения к PostgreSQL.
///     Ошибки server.start — при невозможности запуска HTTP-сервера.
///
/// Memory:
///     После успешной инициализации базы данных регистрируется
///     defer-вызов Database.deinit(), обеспечивающий освобождение
///     ресурсов при выходе из функции.
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
