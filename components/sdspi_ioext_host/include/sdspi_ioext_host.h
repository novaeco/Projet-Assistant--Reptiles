#pragma once

#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*sdspi_ioext_cs_cb_t)(bool assert, void *user_ctx);

typedef struct {
    spi_host_device_t spi_host;
    const spi_bus_config_t *bus_cfg;
    sdspi_device_config_t slot_config;
    uint32_t max_freq_khz;
    sdspi_ioext_cs_cb_t set_cs_cb;
    void *cs_user_ctx;
    uint32_t cs_setup_delay_us;
    uint32_t cs_hold_delay_us;
    uint32_t initial_clocks;
} sdspi_ioext_config_t;

esp_err_t sdspi_ioext_host_init(const sdspi_ioext_config_t *config, sdmmc_host_t *host_out, sdspi_dev_handle_t *device_out);

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device, spi_host_device_t spi_host, bool free_bus);

#ifdef __cplusplus
}
#endif

