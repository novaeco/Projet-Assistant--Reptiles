#include "backlight.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define TAG "backlight"

static ledc_channel_config_t ledc_channel;

esp_err_t backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 1000,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel.gpio_num   = 5;
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.channel    = LEDC_CHANNEL_0;
    ledc_channel.timer_sel  = LEDC_TIMER_0;
    ledc_channel.duty       = 0;
    ledc_channel.hpoint     = 0;
    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Backlight PWM initialized");
    return ESP_OK;
}

void backlight_set(uint8_t level)
{
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, level);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

