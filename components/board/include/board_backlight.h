#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t max_duty;
    bool active_low;
    bool ramp_test;
} board_backlight_init_config_t;

esp_err_t board_backlight_init(const board_backlight_init_config_t *config);

esp_err_t board_backlight_set_percent(uint8_t percent);

#ifdef __cplusplus
}
#endif
