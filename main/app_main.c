#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "backlight.h"
#include "keyboard.h"
#include "touch.h"
#include "buttons.h"
#include "network.h"
#include "storage_sd.h"
#include "power.h"

static const char *TAG = "app_main";

static void hello_task(void *pvParameter)
{
    while (1) {
        ESP_LOGI(TAG, "Hello from LizardNova!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t err;

    err = power_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power_init failed: %s", esp_err_to_name(err));
        return;
    }
    power_high_performance();
    err = backlight_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight_init failed: %s", esp_err_to_name(err));
        return;
    }
    display_config_t disp_cfg = {
        .orientation = DISPLAY_ORIENTATION_PORTRAIT,
        .color_format = DISPLAY_COLOR_FORMAT_RGB565,
    };
    err = display_init(&disp_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = keyboard_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "keyboard_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = buttons_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buttons_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = storage_sd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage_sd_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = network_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network_init failed: %s", esp_err_to_name(err));
        return;
    }

    backlight_set(128);
    xTaskCreate(hello_task, "hello_task", 2048, NULL, 5, NULL);

    while (1) {
        display_update();
        network_update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
