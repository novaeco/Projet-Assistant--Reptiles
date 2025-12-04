#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

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
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "Starting Assistant Administratif Reptiles...");
    ESP_LOGI(TAG, "Reset reason: %d", reset_reason);

    // 1. Initialize Storage (NVS + FS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing storage (NVS + filesystem)...");
    ESP_ERROR_CHECK(storage_init());

    // 2. Initialize Board (IO expander + SD bus first, then LCD/Touch)
    ESP_LOGI(TAG, "Bringing up board peripherals (SD bus before LCD/LVGL, touch, IO expander)...");
    esp_err_t board_err = board_init();
    if (board_err != ESP_OK) {
        ESP_LOGE(TAG, "Board init completed with errors: %s", esp_err_to_name(board_err));
    }

    ESP_LOGI(TAG, "board features enabled: expander=%d touch=%d sd=%d lcd=%d",
             board_has_io_expander(), board_touch_is_ready(), board_sd_is_mounted(), board_lcd_is_ready());

    // Restore backlight level if saved (requires IO expander)
    int32_t backlight_pct = 100;
    if (storage_nvs_get_i32("backlight_pct", &backlight_pct) == ESP_OK) {
        if (backlight_pct < 0) {
            backlight_pct = 0;
        } else if (backlight_pct > 100) {
            backlight_pct = 100;
        }
    }
    esp_err_t bk_err = board_set_backlight_percent((uint8_t)backlight_pct);
    if (bk_err != ESP_OK) {
        ESP_LOGW(TAG, "Backlight restore skipped: %s", esp_err_to_name(bk_err));
    }

    // 3. Initialize Network (WiFi Stack, SNTP)
    ESP_LOGI(TAG, "Starting network stack (netif/event loop/Wi-Fi/SNTP)...");
    ESP_ERROR_CHECK(net_init());

    // 4. Initialize Core Service (Data Models, Integrity Check)
    ESP_LOGI(TAG, "Initializing core services...");
    ESP_ERROR_CHECK(core_init());

    // 5. Initialize UI (LVGL, Display Driver, Input Driver)
    ESP_LOGI(TAG, "Initializing LVGL/UI...");
    ESP_ERROR_CHECK(ui_init());

    // 6. Initialize Web Server
    ESP_LOGI(TAG, "Starting web server...");
    ESP_ERROR_CHECK(web_server_init());

    // 7. Initialize IOT (MQTT)
    // It will attempt to connect once WiFi is up
    ESP_LOGI(TAG, "Starting MQTT client...");
    ESP_ERROR_CHECK(iot_init());

    ESP_LOGI(TAG, "System Initialized Successfully.");

    // Keep app_main alive to avoid returning to RTOS idle
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}