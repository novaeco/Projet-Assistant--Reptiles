#include "board_backlight.h"
#include "board_internal.h"
#include "board_io_map.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_BOARD_BACKLIGHT_MAX_DUTY
#define CONFIG_BOARD_BACKLIGHT_MAX_DUTY 255
#endif

#ifndef CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW
#define CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW 0
#endif

#ifndef CONFIG_BOARD_BACKLIGHT_RAMP_TEST
#define CONFIG_BOARD_BACKLIGHT_RAMP_TEST 0
#endif

static const char *TAG = "BOARD_BK";
static board_backlight_init_config_t s_bl_cfg = {
    .max_duty = CONFIG_BOARD_BACKLIGHT_MAX_DUTY,
    .active_low = CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW,
    .ramp_test = CONFIG_BOARD_BACKLIGHT_RAMP_TEST,
};

static const char *board_backlight_driver_label(board_io_driver_t drv)
{
    switch (drv) {
    case BOARD_IO_DRIVER_WAVESHARE:
        return "Waveshare PWM";
    case BOARD_IO_DRIVER_CH422:
        return "CH422G digital";
    default:
        return "unknown";
    }
}

static esp_err_t board_backlight_set_enables(bool enable_backlight)
{
    esp_err_t err = ESP_OK;

    if (board_internal_has_expander()) {
        esp_err_t vdd_err = board_internal_set_output(IO_EXP_PIN_LCD_VDD, true);
        if (vdd_err != ESP_OK) {
            ESP_LOGW(TAG, "LCD_VDD enable failed: %s", esp_err_to_name(vdd_err));
            err = (err == ESP_OK) ? vdd_err : err;
        }

        esp_err_t bk_err = board_internal_set_output(IO_EXP_PIN_BK, enable_backlight);
        if (bk_err != ESP_OK) {
            ESP_LOGW(TAG, "Backlight enable write failed: %s", esp_err_to_name(bk_err));
            err = (err == ESP_OK) ? bk_err : err;
        }
    }

    return err;
}

esp_err_t board_backlight_set_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    if (!board_internal_has_expander()) {
        ESP_LOGW(TAG, "Backlight unavailable (no IO expander)");
        return ESP_ERR_INVALID_STATE;
    }

    const uint16_t max_duty = s_bl_cfg.max_duty;
    if (max_duty == 0) {
        ESP_LOGE(TAG, "Invalid backlight max_duty=0");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t duty = ((uint32_t)percent * (uint32_t)max_duty + 50u) / 100u;
    if (duty > (uint32_t)max_duty) {
        duty = (uint32_t)max_duty;
    }

    if (s_bl_cfg.active_low) {
        duty = (uint32_t)max_duty - duty;
    }

    bool enable = percent > 0;
    ESP_RETURN_ON_ERROR(board_backlight_set_enables(enable), TAG, "Enable sequence failed");

    uint8_t applied_raw = 0;
    board_io_driver_t driver = board_internal_get_io_driver();
    esp_err_t err = board_internal_set_pwm_raw(duty, max_duty, &applied_raw);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Backlight %u%% -> duty=%u/%u (raw=%u) driver=%s active_low=%d enables:LCD_VDD=on BK=%s",
             percent, duty, max_duty, (unsigned)applied_raw,
             board_backlight_driver_label(driver),
             s_bl_cfg.active_low,
             enable ? "on" : "off");

    return ESP_OK;
}

static void board_backlight_run_ramp(void)
{
    ESP_LOGI(TAG, "Starting backlight ramp test (0->100%% step=10%%)");
    for (int pct = 0; pct <= 100; pct += 10) {
        esp_err_t err = board_backlight_set_percent((uint8_t)pct);
        ESP_LOGI(TAG, "Ramp %3d%% -> %s", pct, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t board_backlight_init(const board_backlight_init_config_t *config)
{
    if (config) {
        s_bl_cfg = *config;
    }

    if (s_bl_cfg.max_duty == 0) {
        ESP_LOGW(TAG, "max_duty=0 from config, restoring default 255");
        s_bl_cfg.max_duty = 255;
    }

    if (!board_internal_has_expander()) {
        ESP_LOGW(TAG, "Backlight init skipped (expander absent)");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Backlight backend=%s max_duty=%u active_low=%d ramp_test=%d",
             board_backlight_driver_label(board_internal_get_io_driver()),
             (unsigned)s_bl_cfg.max_duty,
             s_bl_cfg.active_low,
             s_bl_cfg.ramp_test);

    if (s_bl_cfg.ramp_test) {
        board_backlight_run_ramp();
    }

    esp_err_t apply_err = board_backlight_set_percent(100);
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply default 100%% brightness: %s", esp_err_to_name(apply_err));
    }

    return ESP_OK;
}
