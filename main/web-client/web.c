#include "web.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

char* WIFI_SSID  = "korobochka";
char* WIFI_PASSWORD = "simsim_open";
char* SERVER_URL = "http://91.92.43.183/api/checkUID";

static bool load_wifi_credentials(void);
static bool get_form_value(
    const char *body,
    const char *key,
    char *output,
    size_t output_size
);

static const char setup_page[] =
"<!DOCTYPE html>"
"<html lang='ru'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>RFID Setup</title>"
"<style>"
"*{box-sizing:border-box}"
"body{margin:0;min-height:100vh;display:flex;align-items:center;"
"justify-content:center;background:#0b1020;color:#f2f6ff;"
"font-family:Arial,sans-serif;padding:20px}"
".panel{width:100%;max-width:420px;background:#151f35;"
"border:1px solid #263b59;border-radius:20px;padding:28px;"
"box-shadow:0 15px 50px #0008}"
".logo{color:#35d6f5;font-size:13px;letter-spacing:3px;"
"font-weight:bold;margin-bottom:25px}"
"h1{font-size:25px;margin:0 0 10px}"
".sub{color:#91a1bb;font-size:14px;line-height:1.5;"
"margin-bottom:25px}"
"label{display:block;font-size:13px;color:#b9c9df;margin:18px 0 8px}"
"input{width:100%;padding:14px;border-radius:10px;"
"border:1px solid #344966;background:#0e182b;color:white;"
"font-size:15px;outline:none}"
"input:focus{border-color:#35d6f5}"
"button{width:100%;margin-top:25px;padding:15px;"
"border:0;border-radius:11px;background:#168db5;"
"color:white;font-weight:bold;font-size:15px;cursor:pointer}"
"button:active{transform:scale(.98)}"
".note{font-size:12px;color:#8192aa;margin-top:20px;"
"text-align:center;line-height:1.5}"
"</style>"
"</head>"
"<body>"
"<main class='panel'>"
"<div class='logo'>RFID TERMINAL</div>"
"<h1>Настройка Wi-Fi</h1>"
"<p class='sub'>Укажите сеть, к которой необходимо подключить терминал.</p>"
"<form method='POST' action='/save'>"
"<label>Название сети (SSID)</label>"
"<input name='ssid' maxlength='32' required autocomplete='off' "
"placeholder='Название Wi-Fi'>"
"<label>Пароль сети</label>"
"<input name='password' type='password' maxlength='63' "
"autocomplete='new-password' placeholder='Пароль Wi-Fi'>"
"<button type='submit'>Сохранить и подключиться</button>"
"</form>"
"<div class='note'>Настройки сохраняются в памяти устройства."
"<br>После сохранения терминал перезапустится.</div>"
"</main>"
"</body>"
"</html>";



static const char *TAG = "HTTP_CLIENT";

#define WIFI_CONNECTED_BIT BIT0

#define SETUP_AP_SSID      "RFID-Setup"
#define SETUP_AP_PASSWORD  "rfidsetup"
static bool wifi_has_credentials = false;
static EventGroupHandle_t wifi_event_group;
static char saved_ssid[33];
static char saved_password[65];
static httpd_handle_t setup_server = NULL;


static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(
        req,
        setup_page,
        HTTPD_RESP_USE_STRLEN
    );
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char body[512] = {0};

    if (req->content_len == 0 ||
        req->content_len >= sizeof(body)) {

        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid request");
    }

    size_t received = 0;

    while (received < req->content_len) {
        int ret = httpd_req_recv(
            req,
            body + received,
            req->content_len - received
        );

        if (ret <= 0) {
            return ESP_FAIL;
        }

        received += ret;
    }

    body[received] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};

    if (!get_form_value(
            body,
            "ssid",
            ssid,
            sizeof(ssid)
        ) ||
        !get_form_value(
            body,
            "password",
            password,
            sizeof(password)
        )) {

        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid form data");
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);

    if (ssid_len == 0 || ssid_len > 32) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid SSID");
    }

    if (password_len != 0 &&
        (password_len < 8 || password_len > 63)) {

        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(
            req,
            "Wi-Fi password must contain 8-63 characters or be empty"
        );
    }

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        "wifi_cfg",
        NVS_READWRITE,
        &handle
    );

    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "NVS open failed");
    }

    err = nvs_set_str(handle, "ssid", ssid);

    if (err == ESP_OK) {
        err = nvs_set_str(handle, "password", password);
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi credentials: %s",
                 esp_err_to_name(err));

        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "Failed to save settings");
    }

    ESP_LOGI(TAG, "Wi-Fi credentials saved for: %s", ssid);

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char *response =
        "<html lang='ru'><meta charset='UTF-8'>"
        "<body style='background:#101827;color:white;"
        "font-family:Arial;text-align:center;padding:50px'>"
        "<h2>Настройки сохранены!</h2>"
        "<p>Терминал перезапускается и подключается к новой сети.</p>"
        "</body></html>";

    esp_err_t send_err = httpd_resp_sendstr(req, response);

    if (send_err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
    }

    return send_err;
}

static void start_setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;

    ESP_ERROR_CHECK(
        httpd_start(&setup_server, &config)
    );

    static const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
        .user_ctx = NULL
    };

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(setup_server, &root_uri)
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(setup_server, &save_uri)
    );

    ESP_LOGI(TAG, "Wi-Fi setup page: http://192.168.4.1");
}


esp_err_t sendRequest(RFIDCard uid) {
    if (uid.size == 0 || uid.size > sizeof(uid.bytes)) {
        ESP_LOGE(TAG, "Invalid UID size: %d", uid.size);
        return ESP_ERR_INVALID_ARG;
    }
    EventBits_t bits = xEventGroupWaitBits(
    wifi_event_group,
    WIFI_CONNECTED_BIT,
    pdFALSE,
    pdTRUE,
    pdMS_TO_TICKS(5000)
    );

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi is not connected");
        return ESP_ERR_TIMEOUT;
    }

    char json[128];

    int offset = snprintf(
        json,
        sizeof(json),
        "{\"size\":%d,\"bytes\":[",
        uid.size
    );

    for (int i = 0; i < uid.size; i++) {
        int written = snprintf(
            json + offset,
            sizeof(json) - offset,
            "%s%u",
            i == 0 ? "" : ",",
            uid.bytes[i]
        );

        if (written < 0 || written >= sizeof(json) - offset) {
            ESP_LOGE(TAG, "JSON buffer overflow");
            return ESP_ERR_INVALID_SIZE;
        }

        offset += written;
    }

    snprintf(json + offset, sizeof(json) - offset, "]}");

    ESP_LOGI(TAG, "Sending UID: %s", json);

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(
        esp_http_client_set_method(client, HTTP_METHOD_POST)
    );

    esp_err_t header_err = esp_http_client_set_header(
        client,
        "Content-Type",
        "application/json"
    );

    if (header_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Content-Type: %s",
                esp_err_to_name(header_err));
        esp_http_client_cleanup(client);
        return header_err;
    }

    int json_len = strlen(json);

    esp_err_t post_err = esp_http_client_set_post_field(
        client,
        json,
        json_len
    );

    if (post_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set POST field: %s",
                esp_err_to_name(post_err));
        esp_http_client_cleanup(client);
        return post_err;
    }

    // Проверяем, что HTTP-клиент действительно сохранил тело
    char *post_data = NULL;
    int stored_len = esp_http_client_get_post_field(client, &post_data);

    ESP_LOGI(
        TAG,
        "POST body length: %d, expected: %d",
        stored_len,
        json_len
    );

    if (stored_len != json_len || post_data == NULL) {
        ESP_LOGE(TAG, "POST body was not configured correctly");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_err_t result = ESP_FAIL;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);

        ESP_LOGI(TAG, "HTTP Status = %d", status);

        if (status == 200) {
            result = ESP_OK;
        }
        else {
            ESP_LOGW(TAG, "Server rejected UID. HTTP Status = %d", status);
        }
    }
    else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        result = err;
    }

    esp_http_client_cleanup(client);

    return result;
}



static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        if (wifi_has_credentials) {
            ESP_LOGI(TAG, "Connecting to saved Wi-Fi...");
            esp_wifi_connect();
        }
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        ESP_LOGW(TAG, "Wi-Fi disconnected");

        if (wifi_has_credentials) {
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "Connected! IP: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

void wifi_init_sta(void)
{
    // Network initialization

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        return;
    }

    // Load saved credentials

    wifi_has_credentials = load_wifi_credentials();

    // Initialize Wi-Fi

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL
        )
    );

    // Access point configuration

    wifi_config_t ap_config = {0};

    memcpy(
        ap_config.ap.ssid,
        SETUP_AP_SSID,
        strlen(SETUP_AP_SSID)
    );

    memcpy(
        ap_config.ap.password,
        SETUP_AP_PASSWORD,
        strlen(SETUP_AP_PASSWORD)
    );

    ap_config.ap.ssid_len = strlen(SETUP_AP_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    // AP + STA mode

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_APSTA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(WIFI_IF_AP, &ap_config)
    );

    // Configure station if credentials exist

    if (wifi_has_credentials) {
        wifi_config_t sta_config = {0};

        memcpy(
            sta_config.sta.ssid,
            saved_ssid,
            strlen(saved_ssid)
        );

        memcpy(
            sta_config.sta.password,
            saved_password,
            strlen(saved_password)
        );

        ESP_ERROR_CHECK(
            esp_wifi_set_config(WIFI_IF_STA, &sta_config)
        );

        ESP_LOGI(TAG, "Saved Wi-Fi credentials loaded");
    } else {
        ESP_LOGW(TAG, "No Wi-Fi credentials. Setup mode enabled");
    }

    // Start Wi-Fi

    ESP_ERROR_CHECK(esp_wifi_start());

    // Start configuration web server

    start_setup_server();

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "Wi-Fi setup access point started");
    ESP_LOGI(TAG, "SSID: %s", SETUP_AP_SSID);
    ESP_LOGI(TAG, "Open: http://192.168.4.1");
    ESP_LOGI(TAG, "================================");
}

static bool load_wifi_credentials(void)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        "wifi_cfg",
        NVS_READONLY,
        &handle
    );

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi credentials not found");
        return false;
    }

    size_t ssid_len = sizeof(saved_ssid);

    err = nvs_get_str(
        handle,
        "ssid",
        saved_ssid,
        &ssid_len
    );

    if (err != ESP_OK || saved_ssid[0] == '\0') {
        nvs_close(handle);
        return false;
    }

    size_t password_len = sizeof(saved_password);

    err = nvs_get_str(
        handle,
        "password",
        saved_password,
        &password_len
    );

    if (err != ESP_OK) {
        saved_password[0] = '\0';
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Saved Wi-Fi network: %s", saved_ssid);

    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;

    return -1;
}

static bool get_form_value(
    const char *body,
    const char *key,
    char *output,
    size_t output_size
)
{
    size_t key_len = strlen(key);

    const char *p = body;

    while (*p != '\0') {
        const char *end = strchr(p, '&');

        size_t pair_len = end
            ? (size_t)(end - p)
            : strlen(p);

        if (pair_len > key_len &&
            memcmp(p, key, key_len) == 0 &&
            p[key_len] == '=') {

            const char *value = p + key_len + 1;

            size_t value_len = pair_len - key_len - 1;

            size_t src = 0;
            size_t dst = 0;

            while (src < value_len) {
                char c = value[src++];

                if (c == '+') {
                    c = ' ';
                }
                else if (c == '%') {
                    if (src + 1 >= value_len) {
                        return false;
                    }

                    int high = hex_value(value[src]);
                    int low = hex_value(value[src + 1]);

                    if (high < 0 || low < 0) {
                        return false;
                    }

                    c = (char)((high << 4) | low);
                    src += 2;
                }

                if (c == '\0' || dst + 1 >= output_size) {
                    return false;
                }

                output[dst++] = c;
            }

            output[dst] = '\0';

            return true;
        }

        if (end == NULL) {
            break;
        }

        p = end + 1;
    }

    return false;
}

