#include "power.h"
#include "esp_log.h"
#include "esp_pm.h"

#define TAG "power"

static esp_pm_lock_handle_t s_performance_lock;

void power_init(void)
{
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "perf", &s_performance_lock);
    ESP_LOGI(TAG, "Power management initialized");
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

