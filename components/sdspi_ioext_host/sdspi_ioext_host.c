#include "sdspi_ioext_host.h"
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/soc_caps.h"

static const char *TAG = "sdspi_ioext";

// Opaque in the public headers, but the ESP-IDF implementation keeps the SPI device handle
// as the second field. We mirror the layout locally to safely extract the spi_device_handle_t
// needed for the raw clock train while keeping compatibility with the upstream driver.
typedef struct {
    void *host;                   // sdspi_host_t* (unused here)
    spi_device_handle_t spi;      // underlying SPI device registered on the bus
    int slot;                     // slot identifier managed by sdspi_host
} sdspi_internal_dev_t;

typedef struct {
    sdspi_dev_handle_t device;
    spi_device_handle_t spi_dev;
    sdspi_ioext_cs_cb_t set_cs_cb;
    void *cs_user_ctx;
    uint32_t cs_setup_delay_us;
    uint32_t cs_hold_delay_us;
    uint32_t initial_clocks;
    bool sent_initial_clocks;
    spi_host_device_t host_id;
    bool in_use;
} sdspi_ioext_context_t;

static sdspi_ioext_context_t s_ctx[SOC_SPI_HOST_NUM] = {0};

static inline spi_device_handle_t sdspi_ioext_extract_spi(sdspi_dev_handle_t dev)
{
    if (!dev) {
        return NULL;
    }
    sdspi_internal_dev_t *internal = (sdspi_internal_dev_t *)dev;
    return internal->spi;
}

static sdspi_ioext_context_t *sdspi_ioext_get_ctx(int slot)
{
    if (slot < 0 || slot >= SOC_SPI_HOST_NUM) {
        ESP_LOGW(TAG, "do_transaction invalid slot index %d", slot);
        return NULL;
    }
    sdspi_ioext_context_t *ctx = &s_ctx[slot];
    if (!ctx->in_use) {
        ESP_LOGW(TAG, "do_transaction slot %d not initialized", slot);
        return NULL;
    }
    if (ctx->host_id != slot) {
        ESP_LOGE(TAG, "do_transaction slot mismatch (ctx=%d arg=%d)", ctx->host_id, slot);
        return NULL;
    }
    return ctx;
}

static inline void sdspi_ioext_toggle_cs(sdspi_ioext_context_t *ctx, bool assert)
{
    if (!ctx || !ctx->set_cs_cb) {
        return;
    }

    esp_err_t cs_err = ctx->set_cs_cb(assert, ctx->cs_user_ctx);
    if (cs_err != ESP_OK) {
        ESP_LOGW(TAG, "CS toggle %s failed: %s", assert ? "assert" : "deassert", esp_err_to_name(cs_err));
    }
    ESP_LOGD(TAG, "CS %s via IO expander", assert ? "ASSERT" : "DEASSERT");
}

static esp_err_t sdspi_ioext_send_initial_clocks(sdspi_ioext_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->sent_initial_clocks) {
        return ESP_OK;
    }

    // Ensure CS is deasserted before issuing at least 74 clocks (10 bytes of 0xFF)
    sdspi_ioext_toggle_cs(ctx, false);

    if (!ctx->spi_dev) {
        ESP_LOGE(TAG, "SPI device handle missing for slot %d", ctx->host_id);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t init_bytes[10];
    memset(init_bytes, 0xFF, sizeof(init_bytes));
    spi_transaction_t t = {
        .length = (ctx->initial_clocks ? ctx->initial_clocks : 80),
        .tx_buffer = init_bytes,
    };

    esp_err_t err = spi_device_polling_transmit(ctx->spi_dev, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initial clock train failed: %s", esp_err_to_name(err));
        return err;
    }

    ctx->sent_initial_clocks = true;
    return ESP_OK;
}

static esp_err_t sdspi_ioext_do_transaction(int slot, sdmmc_command_t *cmd)
{
    sdspi_ioext_context_t *ctx = sdspi_ioext_get_ctx(slot);
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "do_transaction(slot=%d, cmd=%d, flags=0x%x)", slot, cmd ? cmd->opcode : -1, cmd ? cmd->flags : 0);

    ESP_RETURN_ON_ERROR(sdspi_ioext_send_initial_clocks(ctx), TAG, "initial clocks failed");
    sdspi_ioext_toggle_cs(ctx, true);
    if (ctx->cs_setup_delay_us) {
        esp_rom_delay_us(ctx->cs_setup_delay_us);
    }

    esp_err_t ret = sdspi_host_do_transaction(ctx->device, cmd);

    if (ctx->cs_hold_delay_us) {
        esp_rom_delay_us(ctx->cs_hold_delay_us);
    }
    sdspi_ioext_toggle_cs(ctx, false);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI transaction failed (slot=%d cmd=%d): %s", slot, cmd ? cmd->opcode : -1, esp_err_to_name(ret));
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

    sdspi_device_config_t slot = {0};
    slot = config->slot_config;
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

    sdspi_ioext_context_t *ctx = &s_ctx[spi_host];
    memset(ctx, 0, sizeof(*ctx));
    ctx->device = device;
    ctx->spi_dev = sdspi_ioext_extract_spi(device);
    ctx->set_cs_cb = config->set_cs_cb;
    ctx->cs_user_ctx = config->cs_user_ctx;
    ctx->cs_setup_delay_us = config->cs_setup_delay_us;
    ctx->cs_hold_delay_us = config->cs_hold_delay_us;
    ctx->initial_clocks = config->initial_clocks ? config->initial_clocks : 80;
    ctx->sent_initial_clocks = false;
    ctx->host_id = spi_host;
    ctx->in_use = true;

    *host_out = host;
    *device_out = device;
    ESP_LOGI(TAG, "sdspi_ioext: init done, host=%d slot=%d, freq=%ukHz, CS via IO expander", spi_host, host.slot,
             host.max_freq_khz);
    return err;
}

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device, spi_host_device_t spi_host, bool free_bus)
{
    if (device) {
        sdspi_host_remove_device(device);
    }
    if (free_bus) {
        spi_bus_free(spi_host);
    }
    if (spi_host >= 0 && spi_host < SOC_SPI_HOST_NUM) {
        memset(&s_ctx[spi_host], 0, sizeof(s_ctx[spi_host]));
    }
    return ESP_OK;
}

