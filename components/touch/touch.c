#include "touch.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "power.h"

#define TAG "touch"

static bool has_touch = false;
#define TOUCH_INT_PIN 4

static void IRAM_ATTR touch_isr(void *arg)
{
    power_register_activity();
}

esp_err_t touch_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 3,
        .scl_io_num = 2,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << TOUCH_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "isr service failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_isr_handler_add(TOUCH_INT_PIN, touch_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "isr add failed: %s", esp_err_to_name(err));
        return err;
    }

    has_touch = true;
    ESP_LOGI(TAG, "Touch controller initialized");
    return ESP_OK;
}

bool touch_read(uint16_t *x, uint16_t *y)
{
    if (!has_touch) return false;
    /* In real implementation read I2C registers */
    *x = 0;
    *y = 0;
    return false;
}

