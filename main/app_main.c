#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

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
    xTaskCreate(hello_task, "hello_task", 2048, NULL, 5, NULL);
}
