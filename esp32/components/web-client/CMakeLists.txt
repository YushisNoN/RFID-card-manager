idf_component_register(SRCS "web-client.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_http_client nvs_flash esp_timer)