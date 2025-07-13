#include "buttons.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "power.h"

#define BUTTON_PIN 1

static void IRAM_ATTR button_isr(void *arg)
{
    power_register_activity();
}

esp_err_t buttons_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE("buttons", "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("buttons", "isr service failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE("buttons", "isr add failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI("buttons", "Initializing buttons");
    return ESP_OK;
}
