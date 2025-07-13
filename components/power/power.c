#include "power.h"
#include "esp_log.h"
#include "esp_pm.h"

#define TAG "power"

static esp_pm_lock_handle_t s_performance_lock;

esp_err_t power_init(void)
{
    esp_err_t err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "perf", &s_performance_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Power management initialized");
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

