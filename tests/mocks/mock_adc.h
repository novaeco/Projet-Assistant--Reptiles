#pragma once
#include "driver/adc.h"

extern int mock_adc1_raw_value;

int adc1_get_raw(adc1_channel_t channel);
esp_err_t adc1_config_width(adc_bits_width_t width);
esp_err_t adc1_config_channel_atten(adc1_channel_t channel, adc_atten_t atten);
