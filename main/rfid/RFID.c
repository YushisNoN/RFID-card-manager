/**
 * @file RFID.c
 * @brief Реализация работы с RFID-считывателем RC522.
 *
 * @details
 * Модуль настраивает SPI-соединение с RC522, запускает считыватель,
 * обрабатывает события обнаружения PICC-карты и передаёт UID карты
 * сетевой подсистеме. Результат обработки отображается через GUI.
 *
 * Текущий сценарий обработки карты:
 * 1. Проверить, что карта перешла в состояние ACTIVE.
 * 2. Вывести информацию о карте и показать экран проверки.
 * 3. Убедиться, что тип карты совместим с MIFARE Classic.
 * 4. Попытаться прочитать блок 1 и выполнить деавторизацию.
 * 5. Скопировать UID карты в структуру RFIDCard.
 * 6. Отправить UID на сервер и показать результат в GUI.
 *
 * @note В текущей реализации ошибка чтения блока 1 записывается в журнал,
 *       но сама по себе не отменяет отправку UID на сервер.
 */

#include "RFID.h"
#include "../web-client/web.h"
#include <esp_log.h>
#include <esp_check.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "gui.h"
#include "rc522.h"
#include "driver/rc522_spi.h"
#include "picc/rc522_mifare.h"

/** Тег ESP-IDF для сообщений журнала RFID-подсистемы. */
static const char *TAG = "rc522-project";

/* -------------------------------------------------------------------------- */
/* Конфигурация подключения RC522                                             */
/* -------------------------------------------------------------------------- */

/** GPIO MISO шины SPI2, подключённый к RC522. */
#define RC522_SPI_BUS_GPIO_MISO    (19)

/** GPIO MOSI шины SPI2, подключённый к RC522. */
#define RC522_SPI_BUS_GPIO_MOSI    (23)

/** GPIO SCLK шины SPI2, подключённый к RC522. */
#define RC522_SPI_BUS_GPIO_SCLK    (18)

/** GPIO выбора SPI-устройства RC522 (SDA/SS/CS). */
#define RC522_SPI_SCANNER_GPIO_SDA (21)

/** GPIO линии сброса RC522. */
#define RC522_SCANNER_GPIO_RST     (22) // soft-reset


/**
 * @brief Конфигурация SPI-подключения RFID-считывателя.
 *
 * @details
 * Считыватель подключён к SPI2_HOST. Параметры шины задаются через
 * `bus_config`, параметры SPI-устройства — через `dev_config`, а линия
 * сброса — через `rst_io_num`.
 *
 * Для изменения разводки платы следует изменить GPIO-макросы выше.
 */
static rc522_spi_config_t driver_config = {
    .host_id = SPI2_HOST,
    .bus_config = &(spi_bus_config_t){
        .miso_io_num = RC522_SPI_BUS_GPIO_MISO,
        .mosi_io_num = RC522_SPI_BUS_GPIO_MOSI,
        .sclk_io_num = RC522_SPI_BUS_GPIO_SCLK,
    },
    .dev_config = {
        .spics_io_num = RC522_SPI_SCANNER_GPIO_SDA,
    },
    .rst_io_num = RC522_SCANNER_GPIO_RST,
};

/** Дескриптор установленного SPI-драйвера RC522. */
static rc522_driver_handle_t driver;

/** Дескриптор экземпляра RFID-считывателя RC522. */
static rc522_handle_t scanner;


/* -------------------------------------------------------------------------- */
/* Вспомогательные функции                                                    */
/* -------------------------------------------------------------------------- */


/**
 * @brief Выводит содержимое одного блока MIFARE в журнал.
 *
 * @param[in] buffer Массив байтов блока размером RC522_MIFARE_BLOCK_SIZE.
 *
 * @details
 * Каждый байт выводится в шестнадцатеричном формате с использованием
 * механизма журналирования ESP-IDF.
 *
 * @note Функция является вспомогательной и в текущем сценарии обработки
 *       карты напрямую не вызывается.
 */
static void dump_block(uint8_t buffer[RC522_MIFARE_BLOCK_SIZE])
{
    for (uint8_t i = 0; i < RC522_MIFARE_BLOCK_SIZE; i++) {
        esp_log_write(ESP_LOG_INFO, TAG, "%02" RC522_X " ", buffer[i]);
    }

    esp_log_write(ESP_LOG_INFO, TAG, "\n");
}



/**
 * @brief Авторизуется и считывает указанный блок MIFARE-карты.
 *
 * @param[in]  scanner        Дескриптор RFID-считывателя.
 * @param[in]  picc           Дескриптор обнаруженной карты.
 * @param[in]  block_address  Адрес блока памяти карты для чтения.
 * @param[out] buffer         Буфер для прочитанных данных размером
 *                             RC522_MIFARE_BLOCK_SIZE.
 *
 * @retval ESP_OK       Блок успешно прочитан.
 * @retval прочая ошибка Код ошибки, возвращённый функцией авторизации
 *                       или чтения MIFARE.
 *
 * @details
 * Для доступа к блоку используется ключ по умолчанию
 * `RC522_MIFARE_KEY_VALUE_DEFAULT`. Сначала выполняется авторизация
 * через `rc522_mifare_auth()`, затем чтение через `rc522_mifare_read()`.
 * При ошибке любой из операций функция немедленно возвращает её код.
 * После успешного чтения содержимое буфера выводится в журнал в HEX.
 *
 * @warning Использование стандартного ключа MIFARE по умолчанию подходит
 *          только для карт, у которых соответствующий сектор не был
 *          перенастроен. Не используйте это как универсальный механизм
 *          доступа к защищённым данным карт.
 */
static esp_err_t read_block(rc522_handle_t scanner, rc522_picc_t *picc, uint8_t block_address, uint8_t buffer[RC522_MIFARE_BLOCK_SIZE])
{
    rc522_mifare_key_t key = {
        .value = { RC522_MIFARE_KEY_VALUE_DEFAULT },
    };

    ESP_RETURN_ON_ERROR(
        rc522_mifare_auth(scanner, picc, block_address, &key),
        TAG,
        "auth fail"
    );

    ESP_RETURN_ON_ERROR(
        rc522_mifare_read(scanner, picc, block_address, buffer),
        TAG,
        "read fail"
    );

    ESP_LOGI(TAG, "Block %u contents:", block_address);
    ESP_LOG_BUFFER_HEX(TAG, buffer, RC522_MIFARE_BLOCK_SIZE);

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Обработка событий RC522                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Обрабатывает изменение состояния обнаруженной PICC-карты.
 *
 * @param[in] arg        Пользовательский контекст обработчика; в текущей
 *                       реализации не используется.
 * @param[in] base       База события ESP-IDF; в текущей реализации
 *                       не используется.
 * @param[in] event_id   Идентификатор события; в текущей реализации
 *                       не используется.
 * @param[in] data       Данные события. Ожидается указатель на
 *                       rc522_picc_state_changed_event_t.
 *
 * @details
 * Обработчик вызывается библиотекой RC522 при изменении состояния карты.
 * События, в которых карта не находится в состоянии
 * `RC522_PICC_STATE_ACTIVE`, игнорируются.
 *
 * Для активной карты обработчик:
 * - выводит сведения о карте;
 * - переводит графический интерфейс в состояние проверки;
 * - проверяет совместимость типа карты с MIFARE Classic;
 * - пытается прочитать блок 1 и выполняет деавторизацию;
 * - копирует UID карты в структуру RFIDCard;
 * - вызывает `sendRequest()` для отправки UID на сервер;
 * - отображает успешный или неуспешный результат.
 *
 * @note Ошибка чтения блока 1 не препятствует отправке UID: результат
 *       `read_block()` только записывается в журнал.
 *
 * @warning Текущий обработчик не содержит защиты от повторной обработки
 *          одной и той же карты при повторных событиях ACTIVE. Если библиотека
 *          генерирует несколько таких событий подряд, сервер может получить
 *          несколько запросов с одним UID.
 */
static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    printf("Detected card!\n");
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = event->picc;

    if (picc->state != RC522_PICC_STATE_ACTIVE) {
        return;
    }

    //Красивая картинка
    /* Вывод информации о карте и обновление состояния GUI. */
    rc522_picc_print(picc);
    gui_show_reading();


    /* Текущий сценарий ограничен картами, совместимыми с MIFARE Classic. */
    if (!rc522_mifare_type_is_classic_compatible(picc->type)) {
        ESP_LOGW(TAG, "Card is not supported by this example");
        gui_show_error();
        return;
    }

    
    /* Чтение блока 1 используется для диагностического/тестового сценария. */
    uint8_t block[RC522_MIFARE_BLOCK_SIZE];
    if (read_block(scanner, picc, 1, block) == ESP_OK) {
        ESP_LOGI(TAG, "Successfully read block 1");
    }
    else {
        ESP_LOGE(TAG, "Failed to read block 1");
    }

    /* Завершение MIFARE-сеанса после попытки чтения блока. */
    if (rc522_mifare_deauth(scanner, picc) != ESP_OK) {
        ESP_LOGW(TAG, "Deauth failed");
    }

    /* Формирование структуры UID для отправки на сервер. */ 
    RFIDCard uid;
    uid.size = picc->uid.length;
    uint8_t l =picc->uid.length;
 
    /* Проверка, что UID помещается в массив RFIDCard.bytes. */
    if (l > sizeof(uid.bytes)) {
        ESP_LOGW(TAG, "UID too long: %u", l);
        gui_show_error();
        return;
    }

     /* Копирование байтов UID из структуры библиотеки RC522. */
    for(uint8_t i = 0; i < l; i++) {
        uid.bytes[i] = picc->uid.value[i];
    }

    /* Отправка UID на сервер и отображение результата. */
    esp_err_t result = sendRequest(uid);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Card successfully accepted");
        gui_show_success();
    }
    else {
        ESP_LOGW(TAG, "Card was not accepted");
        gui_show_error();
    }
    return;
}

/* -------------------------------------------------------------------------- */
/* Публичная инициализация RFID-подсистемы                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Создаёт, настраивает и запускает RFID-считыватель RC522.
 *
 * @details
 * Последовательность инициализации:
 * 1. Инициализируется генератор псевдослучайных чисел.
 * 2. Создаётся SPI-драйвер RC522 с конфигурацией `driver_config`.
 * 3. Устанавливается драйвер RC522.
 * 4. Создаётся экземпляр считывателя с установленным драйвером.
 * 5. Регистрируется `on_picc_state_changed()` для события
 *    `RC522_EVENT_PICC_STATE_CHANGED`.
 * 6. Запускается считыватель вызовом `rc522_start()`.
 *
 * После запуска обнаружение карт выполняется библиотекой RC522,
 * а обработка событий происходит в зарегистрированном callback.
 *
 * @note В текущей реализации возвращаемые значения функций создания,
 *       установки, регистрации события и запуска не проверяются.
 *
 * @return Нет.
 */
void RFIDinit()
{
    srand(time(NULL)); // Initialize random generator

    rc522_spi_create(&driver_config, &driver);
    rc522_driver_install(driver);

    rc522_config_t scanner_config = {
        .driver = driver,
    };

    rc522_create(&scanner_config, &scanner);
    rc522_register_events(scanner, RC522_EVENT_PICC_STATE_CHANGED, on_picc_state_changed, NULL);
    rc522_start(scanner);
    return;
}