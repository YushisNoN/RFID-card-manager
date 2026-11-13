//! # RFID Manager — ZigServer
//!
//! Документация серверной части системы управления RFID-картами.
//!
//! ## Архитектура
//!
//! Сервер состоит из следующих компонентов:
//! - HTTP-сервер для взаимодействия с клиентом и ESP32.
//! - Модуль работы с PostgreSQL.
//! - Модели данных RFID-карт.

/// Точка входа приложения.
pub const application = @import("main.zig");

/// HTTP-сервер и обработка запросов.
pub const server = @import("server.zig");

/// Работа с базой данных PostgreSQL.
pub const database = @import("database/db.zig");

/// Модели данных RFID.
pub const rfid = @import("models/rfid.zig");
