#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "nvs_flash.h"
#include "rfid/RFID.h"
#include "web-client/web.h"
#include "gui/gui.h"

static const char *TAG = "MAIN";
void app_main(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");

    // Initialize GUI
    esp_lcd_panel_handle_t panel = gui_init();

    if (panel == NULL) {
        ESP_LOGE(TAG, "GUI initialization failed");
        return;
    }

    gui_show_waiting();

    ESP_LOGI(TAG, "GUI initialized");

    // Initialize Wi-Fi
    wifi_init_sta();

    ESP_LOGI(TAG, "Wi-Fi initialized");

    // Initialize RFID reader
    RFIDinit();

    ESP_LOGI(TAG, "RFID initialized");

    ESP_LOGI(TAG, "System is ready");
}