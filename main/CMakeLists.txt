idf_component_register(SRCS "main.c" "rfid/RFID.c" "web-client/web.c"
                    INCLUDE_DIRS "." "rfid" "web-client"
                    PRIV_REQUIRES rc522)