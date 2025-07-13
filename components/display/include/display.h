#pragma once

#include "esp_err.h"

/** Initialize ST7789 display and LVGL */
esp_err_t display_init(void);

/** Periodically call from main loop to update LVGL */
void display_update(void);

