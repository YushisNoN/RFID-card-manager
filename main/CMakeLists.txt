idf_component_register(
    SRCS 
        "main.c" 
        "rfid/RFID.c" 
        "web-client/web.c" 
        "gui/gui.c" 
        "gui/rfid_font_18.c"

    INCLUDE_DIRS "." "rfid" "web-client" "gui"

    PRIV_REQUIRES rc522
    
    REQUIRES
        esp_http_client
        esp_http_server
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_lcd 
        driver
        esp_driver_spi
        esp_lvgl_port
    )