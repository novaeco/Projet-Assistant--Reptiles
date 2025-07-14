#pragma once
#include "esp_err.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"

extern int64_t mock_esp_timer_us;
extern int mock_light_sleep_count;
extern int mock_pm_acquire_count;
extern int mock_pm_release_count;

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg1, const char *name, esp_pm_lock_handle_t *out_handle);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle);

esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_ext1_wakeup_mode_t mode);
esp_err_t esp_sleep_enable_touchpad_wakeup(void);
esp_err_t esp_light_sleep_start(void);

int64_t esp_timer_get_time(void);

