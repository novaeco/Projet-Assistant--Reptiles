idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board reptile_storage net core ui web_server iot nvs_flash)