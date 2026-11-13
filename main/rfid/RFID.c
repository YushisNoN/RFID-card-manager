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
//Матвей. Самое важное на 104 строке
static const char *TAG = "rc522-project";

//МАТВЕЙ ТУТ МЕНЯТЬ
#define RC522_SPI_BUS_GPIO_MISO    (19)
#define RC522_SPI_BUS_GPIO_MOSI    (23)
#define RC522_SPI_BUS_GPIO_SCLK    (18)
#define RC522_SPI_SCANNER_GPIO_SDA (21)
#define RC522_SCANNER_GPIO_RST     (22) // soft-reset

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

static rc522_driver_handle_t driver;
static rc522_handle_t scanner;

static void dump_block(uint8_t buffer[RC522_MIFARE_BLOCK_SIZE])
{
    for (uint8_t i = 0; i < RC522_MIFARE_BLOCK_SIZE; i++) {
        esp_log_write(ESP_LOG_INFO, TAG, "%02" RC522_X " ", buffer[i]);
    }

    esp_log_write(ESP_LOG_INFO, TAG, "\n");
}

// Код чтения произвольного блока. МОЖЕТ. Понядобиться
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

    uint8_t read_buffer[RC522_MIFARE_BLOCK_SIZE];

    ESP_LOGI(TAG, "Block %u contents:", block_address);
    ESP_LOG_BUFFER_HEX(TAG, buffer, RC522_MIFARE_BLOCK_SIZE);

    return ESP_OK;
}


static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    printf("Detected card!\n");
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = event->picc;

    if (picc->state != RC522_PICC_STATE_ACTIVE) {
        return;
    }
    //Красивая картинка
    rc522_picc_print(picc);
    gui_show_reading();

    if (!rc522_mifare_type_is_classic_compatible(picc->type)) {
        ESP_LOGW(TAG, "Card is not supported by this example");
        gui_show_error();
        return;
    }
    /*
    if (read(scanner, picc) == ESP_OK) {
        ESP_LOGI(TAG, "Read/Write success");
    }
    else {
        ESP_LOGE(TAG, "Read/Write failed");
    }

    if (rc522_mifare_deauth(scanner, picc) != ESP_OK) {
        ESP_LOGW(TAG, "Deauth failed");
    }
    */

    uint8_t block[RC522_MIFARE_BLOCK_SIZE];
    if (read_block(scanner, picc, 1, block) == ESP_OK) {
        ESP_LOGI(TAG, "Successfully read block 1");
    }
    else {
        ESP_LOGE(TAG, "Failed to read block 1");
    }

    if (rc522_mifare_deauth(scanner, picc) != ESP_OK) {
        ESP_LOGW(TAG, "Deauth failed");
    }

    //То, что написал я. Матвей, самое важное тут!!

    RFIDCard uid;
    uid.size = picc->uid.length;
    uint8_t l =picc->uid.length;
 
    if (l > sizeof(uid.bytes)) {
        ESP_LOGW(TAG, "UID too long: %u", l);
        gui_show_error();
    return;
    }
    for(uint8_t i = 0; i < l; i++) {
        uid.bytes[i] = picc->uid.value[i];
    }
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