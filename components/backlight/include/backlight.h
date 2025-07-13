#pragma once

#include "esp_err.h"

/** Initialize PWM for LCD backlight */
esp_err_t backlight_init(void);

/** Set backlight level (0-255) */
void backlight_set(uint8_t level);

