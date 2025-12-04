#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_backlight_init(void);

esp_err_t board_backlight_set_percent(uint8_t percent);

#ifdef __cplusplus
}
#endif
