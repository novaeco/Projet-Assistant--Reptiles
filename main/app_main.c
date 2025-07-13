#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "backlight.h"
#include "keyboard.h"
#include "touch.h"
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
    power_init();
    backlight_init();
    display_init();
    keyboard_init();
    touch_init();
    storage_sd_init();
    network_init();

    backlight_set(128);
    xTaskCreate(hello_task, "hello_task", 2048, NULL, 5, NULL);

    while (1) {
        display_update();
        network_update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
