#include "board_backlight.h"
#include "board_internal.h"
#include "board_io_map.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#ifndef CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW
#define CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW 0
#endif
#ifndef CONFIG_BOARD_BACKLIGHT_RAMP_TEST
#define CONFIG_BOARD_BACKLIGHT_RAMP_TEST 0
#endif

static const char *TAG = "BOARD_BK";
static bool s_lcd_vdd_supported = true;
static bool s_lcd_vdd_warned_once = false;

#if defined(LEDC_HIGH_SPEED_MODE)
#define BOARD_BACKLIGHT_LEDC_MODE       LEDC_HIGH_SPEED_MODE
#else
#define BOARD_BACKLIGHT_LEDC_MODE       LEDC_LOW_SPEED_MODE
#endif
#define BOARD_BACKLIGHT_LEDC_TIMER      LEDC_TIMER_0
#define BOARD_BACKLIGHT_LEDC_CHANNEL    LEDC_CHANNEL_0
#define BOARD_BACKLIGHT_LEDC_FREQ_HZ    5000
#define BOARD_BACKLIGHT_LEDC_DUTY_RES   LEDC_TIMER_13_BIT

static board_backlight_config_t s_backlight_cfg;

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
    if (!board_internal_has_expander()) {
        return ESP_OK;
    }

    const bool enable_vdd = enable_backlight;
    esp_err_t err = ESP_FAIL;

    if (s_lcd_vdd_supported) {
        // I/O extension is an external I2C device (Waveshare CH32V003).
        // At boot it can occasionally NACK/return invalid replies for the very first transactions.
        // Retry a few times to avoid blocking UI bring-up on a transient condition.
        esp_err_t vdd_err = ESP_FAIL;

        for (int attempt = 1; attempt <= 3; ++attempt) {
            vdd_err = board_internal_set_output(IO_EXP_PIN_LCD_VDD, enable_vdd);
            if (vdd_err == ESP_OK || vdd_err == ESP_ERR_INVALID_RESPONSE) {
                break;
            }

            ESP_LOGW(TAG, "LCD_VDD_EN set failed (%s) attempt %d/3", esp_err_to_name(vdd_err), attempt);
            vTaskDelay(pdMS_TO_TICKS(20 * attempt));
        }

        if (vdd_err == ESP_ERR_INVALID_RESPONSE) {
            s_lcd_vdd_supported = false;
            if (!s_lcd_vdd_warned_once) {
                ESP_LOGW(TAG, "LCD_VDD_EN unsupported/invalid response (%s), continuing", esp_err_to_name(vdd_err));
                s_lcd_vdd_warned_once = true;
            }
            vdd_err = ESP_OK;
        }

        if (vdd_err != ESP_OK) {
            // Non-fatal: keep running; many panels are already powered. Prefer a usable UI.
            ESP_LOGW(TAG, "LCD_VDD_EN still failing: %s (continuing)", esp_err_to_name(vdd_err));
        }
    }

    for (int attempt = 1; attempt <= 3; ++attempt) {
        err = board_internal_set_output(IO_EXP_PIN_BK, enable_backlight);
        if (err == ESP_OK) return ESP_OK;
        ESP_LOGW(TAG, "DISP(BK_EN) set failed (%s) attempt %d/3", esp_err_to_name(err), attempt);
        vTaskDelay(pdMS_TO_TICKS(20 * attempt));
    }

    ESP_LOGE(TAG, "Backlight gate (DISP/BK_EN) failed permanently: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t board_backlight_configure_ledc(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BOARD_BACKLIGHT_LEDC_MODE,
        .duty_resolution = BOARD_BACKLIGHT_LEDC_DUTY_RES,
        .timer_num = BOARD_BACKLIGHT_LEDC_TIMER,
        .freq_hz = BOARD_BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "LEDC timer init failed");

    ledc_channel_config_t channel_cfg = {
        .gpio_num = BOARD_LCD_BK_LIGHT_GPIO,
        .speed_mode = BOARD_BACKLIGHT_LEDC_MODE,
        .channel = BOARD_BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 0 },
    };

    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_cfg), TAG, "LEDC channel init failed");
    return ledc_update_duty(BOARD_BACKLIGHT_LEDC_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL);
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

    const uint16_t max_duty = s_backlight_cfg.max_duty;
    if (max_duty == 0) {
        ESP_LOGE(TAG, "Invalid backlight max_duty=0");
        return ESP_ERR_INVALID_ARG;
    }

    const int ledc_limit = (1 << BOARD_BACKLIGHT_LEDC_DUTY_RES) - 1;
    uint32_t duty = (uint32_t)(((double)percent / 100.0) * (double)max_duty);

    if (duty > max_duty) {
        duty = max_duty;
    }
    if (duty > (uint32_t)ledc_limit) {
        duty = (uint32_t)ledc_limit;
    }

    if (s_backlight_cfg.active_low) {
        duty = (uint32_t)max_duty - duty;
    }

    bool enable = percent > 0;
    ESP_RETURN_ON_ERROR(board_backlight_set_enables(enable), TAG, "Enable sequence failed");

    esp_err_t err = ledc_set_duty(BOARD_BACKLIGHT_LEDC_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(BOARD_BACKLIGHT_LEDC_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Backlight %u%% -> duty=%d/%u driver=%s enables:LCD_VDD=on BK=%s",
                 percent,
                 (int)duty,
                 (unsigned)max_duty,
                 board_backlight_driver_label(board_internal_get_io_driver()),
                 enable ? "on" : "off");
    }

    return err;
}

static esp_err_t board_backlight_run_ramp(void)
{
    const uint8_t step = 5;
    const TickType_t delay_ticks = pdMS_TO_TICKS(25);

    for (uint8_t pct = 0; pct <= 100; pct += step) {
        ESP_RETURN_ON_ERROR(board_backlight_set_percent(pct), TAG, "Ramp-up failed at %u%%", pct);
        vTaskDelay(delay_ticks);
        if (pct > 100 - step) {
            break;
        }
    }

    for (int pct = 100; pct >= 0; pct -= step) {
        ESP_RETURN_ON_ERROR(board_backlight_set_percent((uint8_t)pct), TAG, "Ramp-down failed at %d%%", pct);
        vTaskDelay(delay_ticks);
        if (pct < step) {
            break;
        }
    }

    return ESP_OK;
}

esp_err_t board_backlight_init(const board_backlight_config_t *cfg)
{
    s_backlight_cfg.max_duty = CONFIG_BOARD_BACKLIGHT_MAX_DUTY;
    s_backlight_cfg.active_low = CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW;
    s_backlight_cfg.ramp_test = CONFIG_BOARD_BACKLIGHT_RAMP_TEST;

    if (cfg) {
        s_backlight_cfg = *cfg;
    }

    if (s_backlight_cfg.max_duty == 0) {
        ESP_LOGW(TAG, "max_duty=0 from config, restoring default %d", CONFIG_BOARD_BACKLIGHT_MAX_DUTY);
        s_backlight_cfg.max_duty = CONFIG_BOARD_BACKLIGHT_MAX_DUTY;
        if (s_backlight_cfg.max_duty == 0) {
            s_backlight_cfg.max_duty = 1;
        }
    }

    const int ledc_limit = (1 << BOARD_BACKLIGHT_LEDC_DUTY_RES) - 1;
    if (s_backlight_cfg.max_duty > ledc_limit) {
        ESP_LOGW(TAG, "max_duty=%u exceeds LEDC limit %d, clamping", (unsigned)s_backlight_cfg.max_duty, ledc_limit);
        s_backlight_cfg.max_duty = ledc_limit;
    }

    ESP_RETURN_ON_ERROR(board_backlight_configure_ledc(), TAG, "LEDC setup failed");

    ESP_LOGI(TAG, "Backlight backend=%s max_duty=%u active_low=%s ramp_test=%s",
             board_backlight_driver_label(board_internal_get_io_driver()),
             (unsigned)s_backlight_cfg.max_duty,
             s_backlight_cfg.active_low ? "y" : "n",
             s_backlight_cfg.ramp_test ? "y" : "n");

    ESP_RETURN_ON_ERROR(board_backlight_set_percent(0), TAG, "Failed to apply initial 0%% brightness");

    if (s_backlight_cfg.ramp_test) {
        esp_err_t ramp_err = board_backlight_run_ramp();
        if (ramp_err != ESP_OK) {
            ESP_LOGW(TAG, "Ramp test failed: %s", esp_err_to_name(ramp_err));
        }
    }

    esp_err_t apply_err = board_backlight_set_percent(100);
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply default 100%% brightness: %s", esp_err_to_name(apply_err));
    }

    return ESP_OK;
}
