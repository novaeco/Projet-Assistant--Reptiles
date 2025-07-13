#pragma once
#include "esp_err.h"
#include "driver/spi_master.h"

extern esp_err_t mock_spi_bus_initialize_ret;
extern esp_err_t mock_spi_bus_add_device_ret;
extern esp_err_t mock_spi_device_polling_transmit_ret;

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *bus_config, spi_dma_chan_t dma_chan);
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *dev_config, spi_device_handle_t *handle);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans);
