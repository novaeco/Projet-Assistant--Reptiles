#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

extern esp_err_t mock_gpio_config_ret;
extern esp_err_t mock_gpio_set_level_ret;
extern int mock_gpio_level[40];

esp_err_t gpio_config(const gpio_config_t *pConfig);
esp_err_t gpio_set_level(gpio_num_t gpio_num, int level);
int gpio_get_level(gpio_num_t gpio_num);
