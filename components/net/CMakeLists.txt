idf_component_register(SRCS "src/net_manager.c" "src/net_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event nvs_flash esp_netif esp_http_client esp_http_server esp-tls core reptile_storage)