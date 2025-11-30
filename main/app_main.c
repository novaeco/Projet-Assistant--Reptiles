#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "board.h"
#include "reptile_storage.h"
#include "net_manager.h"
#include "core_service.h"
#include "ui.h"
#include "web_server.h"
#include "iot_manager.h" // Include IOT

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Assistant Administratif Reptiles...");

    // 1. Initialize Storage (NVS + FS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(storage_init());

    // 2. Initialize Board (GPIO, Power, LCD, Touch, SD Card mounting)
    ESP_ERROR_CHECK(board_init());

    // 3. Initialize Network (WiFi Stack, SNTP)
    ESP_ERROR_CHECK(net_init());

    // 4. Initialize Core Service (Data Models, Integrity Check)
    ESP_ERROR_CHECK(core_init());

    // 5. Initialize UI (LVGL, Display Driver, Input Driver)
    ESP_ERROR_CHECK(ui_init());

    // 6. Initialize Web Server
    web_server_init();

    // 7. Initialize IOT (MQTT)
    // It will attempt to connect once WiFi is up
    iot_init();

    // Optional: Check for saved WiFi credentials and connect
    char ssid[32] = {0};
    char pwd[64] = {0};
    size_t len = sizeof(ssid);
    if (storage_nvs_get_str("wifi_ssid", ssid, &len) == ESP_OK && strlen(ssid) > 0) {
        len = sizeof(pwd);
        storage_nvs_get_str("wifi_pwd", pwd, &len);
        ESP_LOGI(TAG, "Found saved WiFi credentials, connecting to %s...", ssid);
        net_connect(ssid, pwd);
    } else {
        ESP_LOGI(TAG, "No saved WiFi credentials found.");
    }

    ESP_LOGI(TAG, "System Initialized Successfully.");
}