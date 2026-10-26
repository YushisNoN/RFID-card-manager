idf_component_register(SRCS "esp32.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES web-client CV 
                    EMBED_FILES ../../photos_resized/test3.jpg)
