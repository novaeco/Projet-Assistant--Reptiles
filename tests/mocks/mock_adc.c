#include "mock_adc.h"

int mock_adc1_raw_value = 0;

int adc1_get_raw(adc1_channel_t channel)
{
    (void)channel;
    return mock_adc1_raw_value;
}

esp_err_t adc1_config_width(adc_bits_width_t width)
{
    (void)width;
    return ESP_OK;
}

esp_err_t adc1_config_channel_atten(adc1_channel_t channel, adc_atten_t atten)
{
    (void)channel;
    (void)atten;
    return ESP_OK;
}
