idf_component_register(SRCS "src/iot_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt esp_https_ota core cjson esp_event nvs_flash reptile_storage)