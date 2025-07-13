#include "touch.h"
#include "esp_log.h"
#include "driver/i2c.h"

#define TAG "touch"

static bool has_touch = false;

void touch_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 3,
        .scl_io_num = 2,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    has_touch = true;
    ESP_LOGI(TAG, "Touch controller initialized");
}

bool touch_read(uint16_t *x, uint16_t *y)
{
    if (!has_touch) return false;
    /* In real implementation read I2C registers */
    *x = 0;
    *y = 0;
    return false;
}

