#include "sdspi_ioext_host.h"
#include <stdint.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/soc_caps.h"
#include "driver/spi_common.h"
#include "freertos/semphr.h"

static const char *TAG = "sdspi_ioext";

typedef struct {
    sdspi_dev_handle_t device;
    spi_device_handle_t clock_dev;
    sdspi_ioext_cs_cb_t set_cs_cb;
    void *cs_user_ctx;
    uint32_t cs_setup_delay_us;
    uint32_t cs_hold_delay_us;
    uint32_t initial_clocks;
    bool sent_initial_clocks;
    spi_host_device_t host_id;
    bool in_use;
    SemaphoreHandle_t lock;
} sdspi_ioext_context_t;

#if defined(SOC_SPI_PERIPH_NUM)
#define SDSPI_IOEXT_MAX_HOSTS (SOC_SPI_PERIPH_NUM)
#elif defined(SPI_HOST_MAX)
#define SDSPI_IOEXT_MAX_HOSTS (SPI_HOST_MAX)
#else
// Fallback for ESP32-S3 and compatible targets
#define SDSPI_IOEXT_MAX_HOSTS (3)
#endif

static sdspi_ioext_context_t s_ctx[SDSPI_IOEXT_MAX_HOSTS] = {0};

static sdspi_ioext_context_t *sdspi_ioext_get_ctx(intptr_t slot)
{
    sdspi_dev_handle_t device = (sdspi_dev_handle_t)slot;

    for (int i = 0; i < SDSPI_IOEXT_MAX_HOSTS; ++i) {
        sdspi_ioext_context_t *ctx = &s_ctx[i];
        if (ctx->in_use && ctx->device == device) {
            return ctx;
        }
    }

    ESP_LOGE(TAG, "do_transaction slot mismatch (got=%p)", (void *)device);
    return NULL;
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

    if (!ctx->clock_dev) {
        ESP_LOGE(TAG, "SPI clock device handle missing for host %d", ctx->host_id);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t init_bytes[10];
    memset(init_bytes, 0xFF, sizeof(init_bytes));
    spi_transaction_t t = {
        .length = (ctx->initial_clocks ? ctx->initial_clocks : 80),
        .tx_buffer = init_bytes,
    };

    esp_err_t err = spi_device_acquire_bus(ctx->clock_dev, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire SPI bus for initial clocks: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_device_polling_transmit(ctx->clock_dev, &t);
    spi_device_release_bus(ctx->clock_dev);
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

    if (ctx->lock) {
        xSemaphoreTake(ctx->lock, portMAX_DELAY);
    }

    ESP_LOGD(TAG, "do_transaction(slot=%p, cmd=%d, flags=0x%x)", (void *)(intptr_t)slot, cmd ? cmd->opcode : -1,
             cmd ? cmd->flags : 0);

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

    if (ctx->lock) {
        xSemaphoreGive(ctx->lock);
    }

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

    sdspi_dev_handle_t device = 0;
    err = sdspi_host_init_device(&slot, &device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI device init failed: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    if (config->max_freq_khz) {
        host.max_freq_khz = config->max_freq_khz;
    }

    host.slot = (int)(intptr_t)device;
    host.do_transaction = sdspi_ioext_do_transaction;

    spi_device_interface_config_t clock_if_cfg = {
        .mode = 0,
        .clock_speed_hz = 400000, // Safe default for initial clocks
        .spics_io_num = -1,
        .queue_size = 1,
        .cs_ena_posttrans = 0,
    };

    spi_device_handle_t clock_dev = NULL;
    err = spi_bus_add_device(spi_host, &clock_if_cfg, &clock_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI clock device: %s", esp_err_to_name(err));
        sdspi_host_remove_device(device);
        return err;
    }

    if (spi_host < 0 || spi_host >= SDSPI_IOEXT_MAX_HOSTS) {
        ESP_LOGE(TAG, "Invalid SPI host %d", spi_host);
        sdspi_host_remove_device(device);
        return ESP_ERR_INVALID_ARG;
    }

    sdspi_ioext_context_t *ctx = &s_ctx[spi_host];
    memset(ctx, 0, sizeof(*ctx));
    ctx->device = device;
    ctx->clock_dev = clock_dev;
    ctx->set_cs_cb = config->set_cs_cb;
    ctx->cs_user_ctx = config->cs_user_ctx;
    ctx->cs_setup_delay_us = config->cs_setup_delay_us;
    ctx->cs_hold_delay_us = config->cs_hold_delay_us;
    ctx->initial_clocks = config->initial_clocks ? config->initial_clocks : 80;
    ctx->sent_initial_clocks = false;
    ctx->host_id = spi_host;
    ctx->in_use = true;
    ctx->lock = xSemaphoreCreateMutex();
    if (!ctx->lock) {
        ESP_LOGW(TAG, "Failed to create SDSPI mutex for host %d, continuing without lock", spi_host);
    }

    *host_out = host;
    *device_out = device;
    ESP_LOGI(TAG, "sdspi_ioext: init done, host=%d device=%p slot=%p, freq=%ukHz, CS via IO expander", spi_host,
             (void *)device, (void *)host.slot, host.max_freq_khz);
    return err;
}

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device, spi_host_device_t spi_host, bool free_bus)
{
    if (spi_host >= 0 && spi_host < SDSPI_IOEXT_MAX_HOSTS) {
        if (s_ctx[spi_host].clock_dev) {
            spi_bus_remove_device(s_ctx[spi_host].clock_dev);
        }
        if (s_ctx[spi_host].lock) {
            vSemaphoreDelete(s_ctx[spi_host].lock);
        }
        memset(&s_ctx[spi_host], 0, sizeof(s_ctx[spi_host]));
    }

    if (device) {
        sdspi_host_remove_device(device);
    }
    if (free_bus) {
        spi_bus_free(spi_host);
    }
    return ESP_OK;
}

