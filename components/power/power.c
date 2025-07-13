#include "power.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "power"

static esp_pm_lock_handle_t s_performance_lock;
static int64_t s_last_activity_us;
static TaskHandle_t s_monitor_task;

#define WAKEUP_GPIO_MASK ((1ULL<<2) | (1ULL<<3) | (1ULL<<4) | (1ULL<<5) | \
                          (1ULL<<6) | (1ULL<<7) | (1ULL<<8) | (1ULL<<9))

esp_err_t power_init(void)
{
    esp_err_t err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "perf", &s_performance_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Power management initialized");

    s_last_activity_us = esp_timer_get_time();
    if (xTaskCreate(inactivity_task, "inactivity_task", 2048, NULL, 5, &s_monitor_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create inactivity task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void power_high_performance(void)
{
    esp_pm_lock_acquire(s_performance_lock);
    ESP_LOGI(TAG, "High performance mode");
}

void power_low_power(void)
{
    esp_pm_lock_release(s_performance_lock);
    ESP_LOGI(TAG, "Low power mode");
}

void power_register_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
}

static void inactivity_task(void *arg)
{
    while (1) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_activity_us > 30 * 1000000LL) {
            power_low_power();
            esp_sleep_enable_ext1_wakeup(WAKEUP_GPIO_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
            esp_sleep_enable_touchpad_wakeup();
            esp_light_sleep_start();
            power_high_performance();
            s_last_activity_us = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


