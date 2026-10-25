#include <stdio.h>
#include "web-client.h"
#include "wifi-manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <sys/param.h>
#include "esp_tls_errors.h"
#include "esp_crt_bundle.h"
//https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/esp_http_client.html ESP offical


#define HOST "httpbin.org"
#define PORT 443
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define USERNAME "Im not so stupid to put my username and password in the code"
#define PASSWORD "your_password"

static const char *TAG = "HTTP_CLIENT";


void web_client_init(){
    wifi_init_sta();
}


//Обработчик эвентов http
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{

    static int output_len;

    switch(evt->event_id) {

        case HTTP_EVENT_ERROR:

            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:

            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT:

            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER:

            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_HEADERS_COMPLETE:

            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADERS_COMPLETE");
            break;

        case HTTP_EVENT_ON_DATA:

            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            //Если начали читать новый ответ то чистим буффер
            if (output_len == 0 && evt->user_data) {

                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);

            }

            int copy_len = 0;

            //user_data наш буффер который мы указали при конфигурации и используем для накопления ответа
            copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len - 1));

            if (copy_len) {
                memcpy(evt->user_data + output_len, evt->data, copy_len);
            }

            output_len += copy_len;

            break;

        case HTTP_EVENT_ON_FINISH:

            //Закончили читать и поэтому все чистим
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");

            if (evt->user_data && output_len > 0) {
                ((char *)evt->user_data)[output_len] = '\0';
            }

            output_len = 0;
            break;

        case HTTP_EVENT_DISCONNECTED:

            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");

            int mbedtls_err = 0;

            esp_err_t err = esp_http_client_get_and_clear_last_tls_error(evt->client, &mbedtls_err, NULL);

            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }

            output_len = 0;
            break;

        default:
            break;

    }

    return ESP_OK;
}



uint8_t check_stud_uid(uint64_t uid){

    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};


    char query_string[64];
    snprintf(query_string, sizeof(query_string), "uid=%" PRIu64, uid);

    esp_http_client_config_t config = {
        .host = HOST,
        .path = "/get",
        .query = query_string,
        .username = USERNAME,
        .password = PASSWORD,  
        .auth_type = HTTP_AUTH_TYPE_BASIC,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer, 
        .transport_type = HTTP_TRANSPORT_OVER_SSL, 
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    uint8_t result = 0;

    // GET
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        if (esp_http_client_get_status_code(client) == 200 && strlen(local_response_buffer) > 0) {

            ESP_LOGI(TAG, "Response: %s", local_response_buffer);

            if (strstr(local_response_buffer, "true") != NULL) {
                result = 1;
            } else {
                result = 0;
            }
        }

    } else {

        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));

    }

    return result;

}

// Наличие отсутсвие плат - битовая маска состояния
void send_state_log(uint32_t state) {

    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};


    char query_string[64];
    snprintf(query_string, sizeof(query_string), "state=%" PRIu32, state);

    esp_http_client_config_t config = {
        .host = HOST,
        .path = "/state",
        .query = query_string,
        .username = USERNAME,
        .password = PASSWORD,  
        .auth_type = HTTP_AUTH_TYPE_BASIC,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer, 
        .transport_type = HTTP_TRANSPORT_OVER_SSL, 
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        if (esp_http_client_get_status_code(client) == 200 && strlen(local_response_buffer) > 0) {

            ESP_LOGI(TAG, "Response: %s", local_response_buffer);

        }

    } else {

        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));

    }
    

}