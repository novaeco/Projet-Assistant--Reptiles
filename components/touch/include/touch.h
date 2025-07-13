#pragma once

#include "esp_err.h"

/** Initialize optional touch controller */
esp_err_t touch_init(void);

/** Read touch coordinates, returns true if touched */
bool touch_read(uint16_t *x, uint16_t *y);

