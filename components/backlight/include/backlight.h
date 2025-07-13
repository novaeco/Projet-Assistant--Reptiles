#pragma once

/** Initialize PWM for LCD backlight */
void backlight_init(void);

/** Set backlight level (0-255) */
void backlight_set(uint8_t level);

