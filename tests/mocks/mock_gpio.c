#include "mock_gpio.h"

esp_err_t mock_gpio_config_ret = ESP_OK;
esp_err_t mock_gpio_set_level_ret = ESP_OK;
int mock_gpio_level[40] = {0};

esp_err_t gpio_config(const gpio_config_t *pConfig)
{
    (void)pConfig;
    return mock_gpio_config_ret;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, int level)
{
    mock_gpio_level[gpio_num] = level;
    return mock_gpio_set_level_ret;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    return mock_gpio_level[gpio_num];
}
