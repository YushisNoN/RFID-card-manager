idf_component_register(SRCS "CV.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32-camera
                    EMBED_FILES server_root_cert.der )
