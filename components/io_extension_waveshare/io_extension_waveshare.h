#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct io_extension_ws *io_extension_ws_handle_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t address;
    uint32_t scl_speed_hz;
    uint8_t retries;
} io_extension_ws_config_t;

esp_err_t io_extension_ws_init(const io_extension_ws_config_t *config, io_extension_ws_handle_t *handle_out);

esp_err_t io_extension_ws_set_output(io_extension_ws_handle_t handle, uint8_t pin, bool level);

esp_err_t io_extension_ws_set_pwm_percent(io_extension_ws_handle_t handle, uint8_t percent);

esp_err_t io_extension_ws_set_pwm_raw(io_extension_ws_handle_t handle, uint8_t duty);

esp_err_t io_extension_ws_read_outputs(io_extension_ws_handle_t handle, uint8_t *value_out);

esp_err_t io_extension_ws_read_inputs(io_extension_ws_handle_t handle, uint8_t *value_out);

esp_err_t io_extension_ws_read_adc(io_extension_ws_handle_t handle, uint16_t *value_out);

esp_err_t io_extension_ws_deinit(io_extension_ws_handle_t handle);

#ifdef __cplusplus
}
#endif

