#include "board_backlight.h"
#include "board_internal.h"
#include "board_io_map.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "BOARD_BK";

#define BOARD_BACKLIGHT_LEDC_MODE       LEDC_HIGH_SPEED_MODE
#define BOARD_BACKLIGHT_LEDC_TIMER      LEDC_TIMER_0
#define BOARD_BACKLIGHT_LEDC_CHANNEL    LEDC_CHANNEL_0
#define BOARD_BACKLIGHT_LEDC_FREQ_HZ    5000
#define BOARD_BACKLIGHT_LEDC_DUTY_RES   LEDC_TIMER_12_BIT

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

    int duty = ((int)percent * (int)max_duty) / 100;
    if (duty < 0) {
        duty = 0;
    }
    if (duty > max_duty) {
        duty = max_duty;
    }

    if (s_backlight_cfg.active_low) {
        duty = max_duty - duty;
    }

    bool enable = percent > 0;
    ESP_RETURN_ON_ERROR(board_backlight_set_enables(enable), TAG, "Enable sequence failed");

    esp_err_t err = ledc_set_duty(BOARD_BACKLIGHT_LEDC_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(BOARD_BACKLIGHT_LEDC_MODE, BOARD_BACKLIGHT_LEDC_CHANNEL);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Backlight %u%% -> duty=%d/%u driver=%s active_low=%d enables:LCD_VDD=on BK=%s",
                 percent,
                 duty,
                 (unsigned)max_duty,
                 board_backlight_driver_label(board_internal_get_io_driver()),
                 s_backlight_cfg.active_low,
                 enable ? "on" : "off");
    }

    return err;
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

    ESP_RETURN_ON_ERROR(board_backlight_configure_ledc(), TAG, "LEDC setup failed");

    ESP_LOGI(TAG, "Backlight backend=%s max_duty=%u active_low=%d ramp_test=%d",
             board_backlight_driver_label(board_internal_get_io_driver()),
             (unsigned)s_backlight_cfg.max_duty,
             s_backlight_cfg.active_low,
             s_backlight_cfg.ramp_test);

    ESP_RETURN_ON_ERROR(board_backlight_set_percent(0), TAG, "Failed to apply initial 0%% brightness");

    if (s_backlight_cfg.ramp_test) {
        for (int pct = 0; pct <= 100; ++pct) {
            board_backlight_set_percent((uint8_t)pct);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        for (int pct = 99; pct >= 0; --pct) {
            board_backlight_set_percent((uint8_t)pct);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    esp_err_t apply_err = board_backlight_set_percent(100);
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply default 100%% brightness: %s", esp_err_to_name(apply_err));
    }

    return ESP_OK;
}
