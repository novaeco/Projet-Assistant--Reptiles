#include <string.h>
#include "mock_i2c.h"

esp_err_t mock_i2c_param_config_ret = ESP_OK;
esp_err_t mock_i2c_driver_install_ret = ESP_OK;
esp_err_t mock_i2c_master_write_read_device_ret = ESP_OK;
uint8_t *mock_i2c_master_read_buffer = NULL;
size_t mock_i2c_master_read_size = 0;

esp_err_t i2c_param_config(i2c_port_t i2c_num, const i2c_config_t *i2c_conf)
{
    (void)i2c_num; (void)i2c_conf;
    return mock_i2c_param_config_ret;
}

esp_err_t i2c_driver_install(i2c_port_t i2c_num, i2c_mode_t mode, size_t slv_rx_buf_len, size_t slv_tx_buf_len, int intr_alloc_flags)
{
    (void)i2c_num; (void)mode; (void)slv_rx_buf_len; (void)slv_tx_buf_len; (void)intr_alloc_flags;
    return mock_i2c_driver_install_ret;
}

esp_err_t i2c_master_write_read_device(i2c_port_t i2c_num, uint8_t device_address, const uint8_t *write_buffer, size_t write_size, uint8_t *read_buffer, size_t read_size, TickType_t ticks_to_wait)
{
    (void)i2c_num; (void)device_address; (void)write_buffer; (void)write_size; (void)ticks_to_wait;
    if (read_buffer && mock_i2c_master_read_buffer && read_size <= mock_i2c_master_read_size) {
        memcpy(read_buffer, mock_i2c_master_read_buffer, read_size);
    }
    return mock_i2c_master_write_read_device_ret;
}
