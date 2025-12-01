idf_component_register(
    SRCS "src/iot_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event esp_wifi esp_netif esp_https_ota core cjson nvs_flash reptile_storage net esp_timer esp_system
)
