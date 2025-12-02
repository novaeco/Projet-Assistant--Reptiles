#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ch422g *ch422g_handle_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    uint8_t address;
    uint32_t scl_speed_hz;
} ch422g_config_t;

esp_err_t ch422g_init(const ch422g_config_t *config, ch422g_handle_t *out_handle);
esp_err_t ch422g_deinit(ch422g_handle_t handle);

esp_err_t ch422g_write_reg(ch422g_handle_t handle, uint8_t reg, uint8_t value);
esp_err_t ch422g_read_reg(ch422g_handle_t handle, uint8_t reg, uint8_t *value_out);

esp_err_t ch422g_set_outputs(ch422g_handle_t handle, uint16_t mask);
esp_err_t ch422g_set_output_level(ch422g_handle_t handle, uint8_t pin, bool level);
esp_err_t ch422g_read_inputs(ch422g_handle_t handle, uint8_t *inputs_out);
uint16_t ch422g_get_cached_outputs(ch422g_handle_t handle);

#ifdef __cplusplus
}
#endif

