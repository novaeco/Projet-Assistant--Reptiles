#include "sdspi_ioext_host.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"
#include "driver/spi_common.h"
#include "freertos/semphr.h"

static const char *TAG = "sdspi_ioext";

static inline uintptr_t sdspi_ioext_handle_value(sdspi_dev_handle_t handle)
{
    return (uintptr_t)handle;
}

typedef struct {
    uint32_t cs_setup_delay_us;
    uint32_t cs_hold_delay_us;
    uint32_t max_freq_khz;
    uint8_t retries;
    uint32_t initial_clocks;
} sdspi_ioext_host_config_t;

typedef struct {
    sdspi_dev_handle_t device;
    spi_device_handle_t clock_dev;
    sdspi_ioext_cs_cb_t set_cs_cb;
    void *cs_user_ctx;
    sdspi_ioext_host_config_t cfg;
    bool sent_initial_clocks;
    spi_host_device_t host_id;
    bool bus_initialized;
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
static SemaphoreHandle_t s_ctx_lock;

static bool sdspi_ioext_lock_ctx_array(TickType_t ticks_to_wait)
{
    if (!s_ctx_lock) {
        s_ctx_lock = xSemaphoreCreateMutex();
        if (!s_ctx_lock) {
            ESP_LOGW(TAG, "Failed to create context array mutex");
            return false;
        }
    }
    if (xSemaphoreTake(s_ctx_lock, ticks_to_wait) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire context array mutex");
        return false;
    }
    return true;
}

static void sdspi_ioext_unlock_ctx_array(void)
{
    if (s_ctx_lock) {
        xSemaphoreGive(s_ctx_lock);
    }
}

static sdspi_ioext_context_t *sdspi_ioext_get_ctx_by_host(spi_host_device_t host)
{
    if (host < 0 || host >= SDSPI_IOEXT_MAX_HOSTS) {
        return NULL;
    }

    sdspi_ioext_context_t *ctx = NULL;
    if (sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
        sdspi_ioext_context_t *candidate = &s_ctx[host];
        if (candidate->in_use) {
            ctx = candidate;
        }
        sdspi_ioext_unlock_ctx_array();
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

    if (!ctx->clock_dev) {
        ESP_LOGE(TAG, "SPI clock device handle missing for host %d", ctx->host_id);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t init_bytes[10];
    memset(init_bytes, 0xFF, sizeof(init_bytes));
    spi_transaction_t t = {
        .length = (ctx->cfg.initial_clocks ? ctx->cfg.initial_clocks : 80),
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
    static bool s_first_transaction_logged = false;

    const spi_host_device_t spi_host = (spi_host_device_t)slot;

    sdspi_ioext_context_t *ctx = sdspi_ioext_get_ctx_by_host(spi_host);
    if (!ctx) {
        ESP_LOGE(TAG, "SDSPI context unavailable for transaction");
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->lock) {
        xSemaphoreTake(ctx->lock, portMAX_DELAY);
    }

    ESP_LOGD(TAG,
             "do_transaction(host=%d cmd=%d flags=0x%x ctx_device=0x%08x)",
             spi_host,
             cmd ? cmd->opcode : -1, cmd ? cmd->flags : 0,
             ctx ? (unsigned)sdspi_ioext_handle_value(ctx->device) : 0);

    if (!s_first_transaction_logged) {
        s_first_transaction_logged = true;
        ESP_LOGD(TAG, "first transaction: host=%d cmd=%p opcode=%d arg=0x%08" PRIx32 " flags=0x%x",
                 spi_host, (void *)cmd,
                 cmd ? cmd->opcode : -1, cmd ? cmd->arg : 0, cmd ? cmd->flags : 0);
    }

    bool cs_asserted = false;
    esp_err_t ret = sdspi_ioext_send_initial_clocks(ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initial clocks failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    sdspi_ioext_toggle_cs(ctx, true);
    cs_asserted = true;
    if (ctx->cfg.cs_setup_delay_us) {
        esp_rom_delay_us(ctx->cfg.cs_setup_delay_us);
    }

    ret = sdspi_host_do_transaction(ctx->device, cmd);

cleanup:
    if (ctx && ctx->cfg.cs_hold_delay_us && cs_asserted) {
        esp_rom_delay_us(ctx->cfg.cs_hold_delay_us);
    }
    if (ctx && cs_asserted) {
        sdspi_ioext_toggle_cs(ctx, false);
    }
    if (ctx->lock) {
        xSemaphoreGive(ctx->lock);
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI transaction failed (host=%d cmd=%d): %s", spi_host,
                 cmd ? cmd->opcode : -1, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t sdspi_ioext_host_init(const sdspi_ioext_config_t *config, sdmmc_host_t *host_out, sdspi_dev_handle_t *device_out)
{
    ESP_RETURN_ON_FALSE(config && host_out && device_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->set_cs_cb, ESP_ERR_INVALID_ARG, TAG, "missing CS callback");

    spi_host_device_t spi_host = config->spi_host;
    esp_err_t err = ESP_OK;
    bool bus_initialized_here = false;
    sdspi_ioext_context_t *ctx = NULL;

    ESP_RETURN_ON_FALSE(spi_host >= 0 && spi_host < SDSPI_IOEXT_MAX_HOSTS, ESP_ERR_INVALID_ARG, TAG,
                        "invalid SPI host id");

    ctx = &s_ctx[spi_host];
    ESP_RETURN_ON_FALSE(sdspi_ioext_lock_ctx_array(portMAX_DELAY), ESP_ERR_NO_MEM, TAG, "ctx mutex unavailable");
    memset(ctx, 0, sizeof(*ctx));
    sdspi_ioext_unlock_ctx_array();

    if (config->bus_cfg) {
        // Mandatory order: initialize SPI bus prior to creating the SDSPI device handle
        err = spi_bus_initialize(spi_host, config->bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
            return err;
        }
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI bus already initialized, reusing existing host %d", spi_host);
            err = ESP_OK;
        } else {
            bus_initialized_here = true;
        }
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config = config->slot_config;
    slot_config.host_id = spi_host;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS; // CS is driven externally through IO expander
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    sdspi_dev_handle_t device = 0;
    err = sdspi_host_init_device(&slot_config, &device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI device init failed: %s", esp_err_to_name(err));
        if (bus_initialized_here) {
            spi_bus_free(spi_host);
        }
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    if (config->max_freq_khz) {
        host.max_freq_khz = config->max_freq_khz;
    }

    host.slot = spi_host;
    host.do_transaction = sdspi_ioext_do_transaction;

    ESP_LOGI(TAG,
             "sdspi_ioext types: sizeof(sdspi_dev_handle_t)=%zu sizeof(int)=%zu sizeof(void*)=%zu device_handle=0x%08x",
             sizeof(sdspi_dev_handle_t), sizeof(int), sizeof(void *), (unsigned)sdspi_ioext_handle_value(device));

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
        if (bus_initialized_here) {
            spi_bus_free(spi_host);
        }
        return err;
    }

    if (spi_host < 0 || spi_host >= SDSPI_IOEXT_MAX_HOSTS) {
        ESP_LOGE(TAG, "Invalid SPI host %d", spi_host);
        sdspi_host_remove_device(device);
        spi_bus_remove_device(clock_dev);
        if (bus_initialized_here) {
            spi_bus_free(spi_host);
        }
        return ESP_ERR_INVALID_ARG;
    }

    if (!sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
        ESP_LOGE(TAG, "Failed to lock context array for host %d", spi_host);
        sdspi_host_remove_device(device);
        spi_bus_remove_device(clock_dev);
        if (bus_initialized_here) {
            spi_bus_free(spi_host);
        }
        return ESP_ERR_TIMEOUT;
    }

    ctx->device = device;
    ctx->clock_dev = clock_dev;
    ctx->set_cs_cb = config->set_cs_cb;
    ctx->cs_user_ctx = config->cs_user_ctx;
    ctx->cfg.cs_setup_delay_us = config->cs_setup_delay_us ? config->cs_setup_delay_us : 5; // enforce a minimal CS setup time
    ctx->cfg.cs_hold_delay_us = config->cs_hold_delay_us ? config->cs_hold_delay_us : 5;   // enforce a minimal CS hold time
    ctx->cfg.initial_clocks = config->initial_clocks ? config->initial_clocks : 80;
    ctx->cfg.max_freq_khz = config->max_freq_khz;
    ctx->sent_initial_clocks = false;
    ctx->host_id = spi_host;
    ctx->bus_initialized = bus_initialized_here;
    ctx->in_use = true;
    ctx->lock = xSemaphoreCreateMutex();
    if (!ctx->lock) {
        ESP_LOGW(TAG, "Failed to create SDSPI mutex for host %d, continuing without lock", spi_host);
    }
    sdspi_ioext_unlock_ctx_array();

    *host_out = host;
    *device_out = device;
    ESP_LOGI(TAG,
             "sdspi_ioext: init done host=%d device_handle=0x%08x host.slot=%" PRIi32
             " freq=%ukHz cs_setup=%uus cs_hold=%uus",
             spi_host, (unsigned)sdspi_ioext_handle_value(device), (int32_t)host.slot,
             host.max_freq_khz, (unsigned)ctx->cfg.cs_setup_delay_us, (unsigned)ctx->cfg.cs_hold_delay_us);
    return err;
}

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device, spi_host_device_t spi_host, bool free_bus)
{
    bool bus_owned = false;
    spi_device_handle_t clock_dev = NULL;
    SemaphoreHandle_t ctx_lock = NULL;
    sdspi_ioext_context_t *ctx = NULL;
    bool took_ctx_lock = false;
    sdspi_dev_handle_t device_handle = device;

    if (spi_host >= 0 && spi_host < SDSPI_IOEXT_MAX_HOSTS && sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
        ctx = &s_ctx[spi_host];
        bus_owned = ctx->bus_initialized;
        clock_dev = ctx->clock_dev;
        ctx_lock = ctx->lock;
        if (device_handle == 0) {
            device_handle = ctx->device;
        }
        ctx->in_use = false; // prevent new lookups while tearing down
        sdspi_ioext_unlock_ctx_array();
    }

    if (ctx_lock) {
        xSemaphoreTake(ctx_lock, portMAX_DELAY);
        took_ctx_lock = true;
    }

    if (clock_dev) {
        spi_bus_remove_device(clock_dev);
    }

    if (spi_host >= 0 && spi_host < SDSPI_IOEXT_MAX_HOSTS && sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
        if (ctx_lock && took_ctx_lock) {
            xSemaphoreGive(ctx_lock);
            vSemaphoreDelete(ctx_lock);
        }
        if (ctx) {
            memset(ctx, 0, sizeof(*ctx));
        }
        memset(&s_ctx[spi_host], 0, sizeof(s_ctx[spi_host]));
        sdspi_ioext_unlock_ctx_array();
    }

    if (device_handle != 0) {
        sdspi_host_remove_device(device_handle);
    }
    if (free_bus && bus_owned) {
        spi_bus_free(spi_host);
    }
    return ESP_OK;
}

