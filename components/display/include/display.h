#pragma once

#include "esp_err.h"

typedef enum {
    DISPLAY_ORIENTATION_PORTRAIT,
    DISPLAY_ORIENTATION_LANDSCAPE,
} display_orientation_t;

typedef enum {
    DISPLAY_COLOR_FORMAT_RGB565,
    DISPLAY_COLOR_FORMAT_RGB666,
} display_color_format_t;

typedef struct {
    display_orientation_t orientation;
    display_color_format_t color_format;
} display_config_t;

/** Initialize ST7789 display and LVGL */
esp_err_t display_init(const display_config_t *config);

/** Periodically call from main loop to update LVGL */
void display_update(void);

