idf_component_register(SRCS "cam.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_driver_gpio REQUIRES esp32-camera) 
