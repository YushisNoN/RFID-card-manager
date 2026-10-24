#include <stdio.h>
#include "esp_timer.h" 
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
#define TIMER_WAIT 5 * 1000000

static int s_retry_num = 0;

static esp_timer_handle_t reconnect_timer = NULL;

// Колбек для таймера 
static void reconnect_timer_callback(void* arg)
{
    esp_wifi_connect();
}

//Обрабочтки событий для эвентов связанных с Wifi
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {

        ESP_ERROR_CHECK(esp_wifi_connect());

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (s_retry_num < MAX_WIFI_RECONNECT) {

            ESP_ERROR_CHECK(esp_wifi_connect());
            s_retry_num++;
            
        } else {

           esp_timer_start_once(reconnect_timer, TIMER_WAIT);
           s_retry_num = 0;

        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        s_retry_num = 0;

    }
}


// Подробно описано вот тут https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/wifi-driver/station-scenarios.html#wifi-station-general-scenario
void wifi_init_sta(void)
{

    //Это таймер для обработки переподключений
    const esp_timer_create_args_t timer_args = {
    .callback = &reconnect_timer_callback,
    .name = "wifi_reconnect_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reconnect_timer));

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


    //Задаем события и обработчик перед началом работы
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));



    //Стартуем
    ESP_ERROR_CHECK(esp_wifi_start());


}
