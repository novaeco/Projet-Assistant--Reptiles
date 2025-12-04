#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_IO_DRIVER_NONE = 0,
    BOARD_IO_DRIVER_WAVESHARE,
    BOARD_IO_DRIVER_CH422,
} board_io_driver_t;

board_io_driver_t board_internal_get_io_driver(void);
bool board_internal_has_expander(void);
esp_err_t board_internal_set_output(uint8_t pin, bool level);
esp_err_t board_internal_set_pwm_raw(uint16_t duty, uint16_t max_duty, uint8_t *applied_raw_out);

#ifdef __cplusplus
}
#endif
