#pragma once

#include "esp_err.h"

/** Initialize matrix keyboard scanning */
esp_err_t keyboard_init(void);

/** Retrieve latest key state as bitmask */
uint16_t keyboard_get_state(void);

