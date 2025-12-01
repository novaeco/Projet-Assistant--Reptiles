idf_component_register(
    SRCS "src/net_manager.c" "src/net_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif esp_http_client esp_http_server esp-tls esp_timer nvs_flash lwip
    PRIV_REQUIRES core reptile_storage cjson
)
