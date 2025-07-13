#pragma once

#include "esp_err.h"

/** Initialize optional touch controller */
esp_err_t touch_init(void);

/** Read touch coordinates, returns true if touched */
bool touch_read(uint16_t *x, uint16_t *y);

typedef struct {
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
} touch_calibration_t;

esp_err_t touch_calibration_load(touch_calibration_t *cal);
esp_err_t touch_calibration_save(const touch_calibration_t *cal);


