#include "sdspi_ioext_host.h"
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "sdspi_ioext";

typedef struct {
    sdspi_dev_handle_t device;
    sdspi_ioext_cs_cb_t set_cs_cb;
    void *cs_user_ctx;
    uint32_t cs_setup_delay_us;
    uint32_t cs_hold_delay_us;
    uint32_t initial_clocks;
    bool sent_initial_clocks;
} sdspi_ioext_context_t;

static sdspi_ioext_context_t s_ctx = {0};

static esp_err_t sdspi_ioext_send_initial_clocks(void)
{
    if (s_ctx.sent_initial_clocks) {
        return ESP_OK;
    }

    // Ensure CS is deasserted before issuing at least 74 clocks (10 bytes of 0xFF)
    if (s_ctx.set_cs_cb) {
        s_ctx.set_cs_cb(false, s_ctx.cs_user_ctx);
    }

    uint8_t init_bytes[10];
    memset(init_bytes, 0xFF, sizeof(init_bytes));
    spi_transaction_t t = {
        .length = (s_ctx.initial_clocks ? s_ctx.initial_clocks : 80),
        .tx_buffer = init_bytes,
    };

    esp_err_t err = spi_device_polling_transmit((spi_device_handle_t)s_ctx.device, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initial clock train failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ctx.sent_initial_clocks = true;
    return ESP_OK;
}

static esp_err_t sdspi_ioext_do_transaction(sdspi_dev_handle_t handle, sdmmc_command_t *cmd)
{
    if (!handle || handle != s_ctx.device) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(sdspi_ioext_send_initial_clocks(), TAG, "initial clocks failed");

    if (s_ctx.set_cs_cb) {
        s_ctx.set_cs_cb(true, s_ctx.cs_user_ctx);
    }
    if (s_ctx.cs_setup_delay_us) {
        esp_rom_delay_us(s_ctx.cs_setup_delay_us);
    }

    esp_err_t ret = sdspi_host_do_transaction(handle, cmd);

    if (s_ctx.cs_hold_delay_us) {
        esp_rom_delay_us(s_ctx.cs_hold_delay_us);
    }
    if (s_ctx.set_cs_cb) {
        s_ctx.set_cs_cb(false, s_ctx.cs_user_ctx);
    }

    return ret;
}

esp_err_t sdspi_ioext_host_init(const sdspi_ioext_config_t *config, sdmmc_host_t *host_out, sdspi_dev_handle_t *device_out)
{
    ESP_RETURN_ON_FALSE(config && host_out && device_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->set_cs_cb, ESP_ERR_INVALID_ARG, TAG, "missing CS callback");

    spi_host_device_t spi_host = config->spi_host;
    esp_err_t err = ESP_OK;

    if (config->bus_cfg) {
        err = spi_bus_initialize(spi_host, config->bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
            return err;
        }
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI bus already initialized, reusing existing host %d", spi_host);
            err = ESP_OK;
        }
    }

    sdspi_device_config_t slot = config->slot_config;
    slot.host_id = spi_host;
    slot.gpio_cs = SDSPI_SLOT_NO_CS;
    slot.gpio_int = SDSPI_SLOT_NO_INT;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    if (config->max_freq_khz) {
        host.max_freq_khz = config->max_freq_khz;
    }

    host.slot = spi_host;
    host.do_transaction = sdspi_ioext_do_transaction;

    sdspi_dev_handle_t device = 0;
    err = sdspi_host_init_device(&slot, &device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI device init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ctx.device = device;
    s_ctx.set_cs_cb = config->set_cs_cb;
    s_ctx.cs_user_ctx = config->cs_user_ctx;
    s_ctx.cs_setup_delay_us = config->cs_setup_delay_us;
    s_ctx.cs_hold_delay_us = config->cs_hold_delay_us;
    s_ctx.initial_clocks = config->initial_clocks ? config->initial_clocks : 80;
    s_ctx.sent_initial_clocks = false;

    *host_out = host;
    *device_out = device;
    return ESP_OK;
}

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device, spi_host_device_t spi_host, bool free_bus)
{
    if (device) {
        sdspi_host_remove_device(device);
    }
    if (free_bus) {
        spi_bus_free(spi_host);
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    return ESP_OK;
}

