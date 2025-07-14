#include "mock_esp.h"

int64_t mock_esp_timer_us = 0;
int mock_light_sleep_count = 0;
int mock_pm_acquire_count = 0;
int mock_pm_release_count = 0;

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg1, const char *name, esp_pm_lock_handle_t *out_handle)
{
    (void)lock_type; (void)arg1; (void)name;
    if (out_handle) *out_handle = (esp_pm_lock_handle_t)0x1;
    return ESP_OK;
}

esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle)
{
    (void)handle;
    mock_pm_acquire_count++;
    return ESP_OK;
}

esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle)
{
    (void)handle;
    mock_pm_release_count++;
    return ESP_OK;
}

esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_ext1_wakeup_mode_t mode)
{
    (void)mask; (void)mode;
    return ESP_OK;
}

esp_err_t esp_sleep_enable_touchpad_wakeup(void)
{
    return ESP_OK;
}

esp_err_t esp_light_sleep_start(void)
{
    mock_light_sleep_count++;
    return ESP_OK;
}

int64_t esp_timer_get_time(void)
{
    return mock_esp_timer_us;
}

