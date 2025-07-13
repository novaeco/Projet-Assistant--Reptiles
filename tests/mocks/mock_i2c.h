#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

extern esp_err_t mock_i2c_param_config_ret;
extern esp_err_t mock_i2c_driver_install_ret;
extern esp_err_t mock_i2c_master_write_read_device_ret;
extern uint8_t *mock_i2c_master_read_buffer;
extern size_t mock_i2c_master_read_size;

esp_err_t i2c_param_config(i2c_port_t i2c_num, const i2c_config_t *i2c_conf);
esp_err_t i2c_driver_install(i2c_port_t i2c_num, i2c_mode_t mode, size_t slv_rx_buf_len, size_t slv_tx_buf_len, int intr_alloc_flags);
esp_err_t i2c_master_write_read_device(i2c_port_t i2c_num, uint8_t device_address, const uint8_t *write_buffer, size_t write_size, uint8_t *read_buffer, size_t read_size, TickType_t ticks_to_wait);
