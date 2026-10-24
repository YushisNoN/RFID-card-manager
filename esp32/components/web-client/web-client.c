#include <stdio.h>
#include "web-client.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <assert.h>
//https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/esp_http_client.html ESP offical


#define HOST "aboba"
#define PORT 80


#define WIFI_SSID "START_1379"
#define WIFI_PASSWORD "89215902115"
#define MAX_WIFI_RECONNECT 5

//НЕОБХОДИМО ДОБАВИТЬ КОЛБЭК ДЛЯ ПЕРЕПОДКЛЮЧЕНИЯ!!!!


// Подробно описано вот тут https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/wifi-driver/station-scenarios.html#wifi-station-general-scenario
void wifi_init_sta(void)
{

    //Эта фигня трубет использования особой памяти
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    //Создает TCP/IP стек и эвент луп для Wifi клиента
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    //Инициализация лоу драйвера Wi-Fi 
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    //Выбирает режим клиента
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));


    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };


    //Выбираем режим и опять задаем конфиг
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    //Стартуем
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

}
