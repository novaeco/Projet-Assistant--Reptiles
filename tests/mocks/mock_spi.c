#include "mock_spi.h"

esp_err_t mock_spi_bus_initialize_ret = ESP_OK;
esp_err_t mock_spi_bus_add_device_ret = ESP_OK;
esp_err_t mock_spi_device_polling_transmit_ret = ESP_OK;

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *bus_config, spi_dma_chan_t dma_chan)
{
    (void)host; (void)bus_config; (void)dma_chan;
    return mock_spi_bus_initialize_ret;
}

esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *dev_config, spi_device_handle_t *handle)
{
    (void)host; (void)dev_config;
    if (handle) *handle = (spi_device_handle_t)0x1;
    return mock_spi_bus_add_device_ret;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans)
{
    (void)handle; (void)trans;
    return mock_spi_device_polling_transmit_ret;
}
