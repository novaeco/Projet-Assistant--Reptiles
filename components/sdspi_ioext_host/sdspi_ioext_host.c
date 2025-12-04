#include "sdspi_ioext_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "sdspi_ioext";
static const sdspi_dev_handle_t SDSPI_IOEXT_INVALID_DEVICE = (sdspi_dev_handle_t)-1;

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
  int ctx_index;
  bool bus_initialized;
  bool in_use;
  SemaphoreHandle_t lock;
} sdspi_ioext_context_t;

// Use the number of SPI peripherals as an upper bound for simultaneously
// tracked contexts
#if defined(SOC_SPI_PERIPH_NUM)
#define SDSPI_IOEXT_MAX_CTX (SOC_SPI_PERIPH_NUM)
#elif defined(SPI_HOST_MAX)
#define SDSPI_IOEXT_MAX_CTX (SPI_HOST_MAX)
#else
// Fallback for ESP32-S3 and compatible targets
#define SDSPI_IOEXT_MAX_CTX (3)
#endif

static sdspi_ioext_context_t s_ctx_table[SDSPI_IOEXT_MAX_CTX] = {0};
static SemaphoreHandle_t s_ctx_lock;

static bool sdspi_ioext_lock_ctx_array(TickType_t ticks_to_wait) {
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

static void sdspi_ioext_unlock_ctx_array(void) {
  if (s_ctx_lock) {
    xSemaphoreGive(s_ctx_lock);
  }
}

static sdspi_ioext_context_t *
sdspi_ioext_get_ctx_by_device(sdspi_dev_handle_t device) {
  if ((int)device < 0) {
    return NULL;
  }

  sdspi_ioext_context_t *ctx = NULL;
  if (sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
    for (size_t i = 0; i < SDSPI_IOEXT_MAX_CTX; ++i) {
      sdspi_ioext_context_t *candidate = &s_ctx_table[i];
      if (candidate->in_use && candidate->device == device) {
        ctx = candidate;
        break;
      }
    }
    sdspi_ioext_unlock_ctx_array();
  }

  return ctx;
}

static sdspi_ioext_context_t *sdspi_ioext_alloc_ctx(int *out_handle) {
  if (!out_handle) {
    return NULL;
  }

  sdspi_ioext_context_t *ctx = NULL;

  if (sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
    for (size_t i = 0; i < SDSPI_IOEXT_MAX_CTX; ++i) {
      sdspi_ioext_context_t *candidate = &s_ctx_table[i];
      if (!candidate->in_use) {
        memset(candidate, 0, sizeof(*candidate));
        candidate->in_use = true;
        candidate->ctx_index = (int)(i + 1);
        ctx = candidate;
        *out_handle = candidate->ctx_index;
        break;
      }
    }
    sdspi_ioext_unlock_ctx_array();
  }

  return ctx;
}

static inline void sdspi_ioext_toggle_cs(sdspi_ioext_context_t *ctx,
                                         bool assert) {
  if (!ctx || !ctx->set_cs_cb) {
    return;
  }

  esp_err_t cs_err = ctx->set_cs_cb(assert, ctx->cs_user_ctx);
  if (cs_err != ESP_OK) {
    ESP_LOGW(TAG, "CS toggle %s failed: %s", assert ? "assert" : "deassert",
             esp_err_to_name(cs_err));
  }
  ESP_LOGD(TAG, "CS %s via IO expander", assert ? "ASSERT" : "DEASSERT");
}

static esp_err_t sdspi_ioext_send_initial_clocks(sdspi_ioext_context_t *ctx) {
  if (!ctx) {
    return ESP_ERR_INVALID_ARG;
  }

  if (ctx->sent_initial_clocks) {
    return ESP_OK;
  }

  // Ensure CS is deasserted before issuing at least 74 clocks (10 bytes of
  // 0xFF)
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
    ESP_LOGE(TAG, "Failed to acquire SPI bus for initial clocks: %s",
             esp_err_to_name(err));
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

static esp_err_t sdspi_ioext_do_transaction(int slot, sdmmc_command_t *cmd) {
  static bool s_first_transaction_logged = false;

  ESP_LOGD(TAG, "do_transaction entry slot=%d (0x%08x)", slot, slot);

  const sdspi_dev_handle_t dev = (sdspi_dev_handle_t)slot;

  sdspi_ioext_context_t *ctx = sdspi_ioext_get_ctx_by_device(dev);
  if (!ctx) {
    ESP_LOGE(TAG, "SDSPI context unavailable for transaction (slot=%d)", slot);
    return ESP_ERR_INVALID_STATE;
  }

  if ((int)ctx->device < 0) {
    ESP_LOGE(TAG, "SDSPI device handle missing for slot %d (host=%d)", slot,
             ctx->host_id);
    return ESP_ERR_INVALID_STATE;
  }

  if (ctx->lock) {
    xSemaphoreTake(ctx->lock, portMAX_DELAY);
  }

  ESP_LOGD(TAG, "do_transaction(host=%d cmd=%d flags=0x%x ctx_device=%d)",
           ctx->host_id, cmd ? cmd->opcode : -1, cmd ? cmd->flags : 0,
           (int)ctx->device);

  if (!s_first_transaction_logged) {
    s_first_transaction_logged = true;
    ESP_LOGD(TAG,
             "first transaction: host=%d cmd=%p opcode=%d arg=0x%08" PRIx32
             " flags=0x%x",
             ctx->host_id, (void *)cmd, cmd ? cmd->opcode : -1,
             cmd ? cmd->arg : 0, cmd ? cmd->flags : 0);
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
    ESP_LOGW(TAG, "SPI transaction failed (host=%d cmd=%d): %s", ctx->host_id,
             cmd ? cmd->opcode : -1, esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t sdspi_ioext_host_init(const sdspi_ioext_config_t *config,
                                sdmmc_host_t *host_out,
                                sdspi_dev_handle_t *device_out) {
  ESP_RETURN_ON_FALSE(config && host_out && device_out, ESP_ERR_INVALID_ARG,
                      TAG, "invalid args");
  ESP_RETURN_ON_FALSE(config->set_cs_cb, ESP_ERR_INVALID_ARG, TAG,
                      "missing CS callback");

  const spi_host_device_t host_id = config->spi_host;
  esp_err_t err = ESP_OK;
  bool bus_initialized_here = false;
  sdspi_ioext_context_t *ctx = NULL;

  ESP_RETURN_ON_FALSE(host_id >= 0 && host_id < SDSPI_IOEXT_MAX_CTX,
                      ESP_ERR_INVALID_ARG, TAG, "invalid SPI host id");

  if (config->bus_cfg) {
    // Mandatory order: initialize SPI bus prior to creating the SDSPI device
    // handle
    err = spi_bus_initialize(host_id, config->bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
      return err;
    }
    if (err == ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "SPI bus already initialized, reusing existing host %d",
               host_id);
      err = ESP_OK;
    } else {
      bus_initialized_here = true;
    }
  }

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config = config->slot_config;
  slot_config.host_id = host_id;
  slot_config.gpio_cs =
      SDSPI_SLOT_NO_CS; // CS is driven externally through IO expander
  slot_config.gpio_int = SDSPI_SLOT_NO_INT;

  sdspi_dev_handle_t device_handle = SDSPI_IOEXT_INVALID_DEVICE;
  err = sdspi_host_init_device(&slot_config, &device_handle);
  if (err != ESP_OK || (int)device_handle < 0) {
    ESP_LOGE(TAG, "SDSPI device init failed: %s", esp_err_to_name(err));
    if (bus_initialized_here) {
      spi_bus_free(host_id);
    }
    return (err != ESP_OK) ? err : ESP_ERR_INVALID_STATE;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  if (config->max_freq_khz) {
    host.max_freq_khz = config->max_freq_khz;
  }

  host.slot = (int)device_handle;
  host.do_transaction = sdspi_ioext_do_transaction;

  spi_device_interface_config_t clock_if_cfg = {
      .mode = 0,
      .clock_speed_hz = 400000, // Safe default for initial clocks
      .spics_io_num = -1,
      .queue_size = 1,
      .cs_ena_posttrans = 0,
  };

  spi_device_handle_t spi_dev = NULL;
  err = spi_bus_add_device(host_id, &clock_if_cfg, &spi_dev);
  if (err != ESP_OK || spi_dev == NULL) {
    ESP_LOGE(TAG, "Failed to add SPI clock device: %s", esp_err_to_name(err));
    sdspi_host_remove_device(device_handle);
    if (bus_initialized_here) {
      spi_bus_free(host_id);
    }
    return (err != ESP_OK) ? err : ESP_ERR_INVALID_STATE;
  }

  int ctx_handle = 0;
  ctx = sdspi_ioext_alloc_ctx(&ctx_handle);
  if (!ctx) {
    ESP_LOGE(TAG, "Failed to allocate SDSPI context for host %d", host_id);
    sdspi_host_remove_device(device_handle);
    spi_bus_remove_device(spi_dev);
    if (bus_initialized_here) {
      spi_bus_free(host_id);
    }
    return ESP_ERR_NO_MEM;
  }

  ctx->device = device_handle;
  ctx->clock_dev = spi_dev;
  ctx->set_cs_cb = config->set_cs_cb;
  ctx->cs_user_ctx = config->cs_user_ctx;
  ctx->cfg.cs_setup_delay_us = config->cs_setup_delay_us
                                   ? config->cs_setup_delay_us
                                   : 5; // enforce a minimal CS setup time
  ctx->cfg.cs_hold_delay_us = config->cs_hold_delay_us
                                  ? config->cs_hold_delay_us
                                  : 5; // enforce a minimal CS hold time
  ctx->cfg.initial_clocks =
      config->initial_clocks ? config->initial_clocks : 80;
  ctx->cfg.max_freq_khz = config->max_freq_khz;
  ctx->sent_initial_clocks = false;
  ctx->host_id = host_id;
  ctx->bus_initialized = bus_initialized_here;
  ctx->lock = xSemaphoreCreateMutex();
  if (!ctx->lock) {
    ESP_LOGW(
        TAG,
        "Failed to create SDSPI mutex for host %d, continuing without lock",
        host_id);
  }

  host.slot = (int)device_handle;
  *host_out = host;
  *device_out = device_handle;
  ESP_LOGI(TAG,
           "sdspi_ioext: init done host=%d device_handle=%d clock_dev=%p "
           "host.slot=%" PRIi32 " freq=%ukHz cs_setup=%uus cs_hold=%uus",
           host_id, (int)device_handle, (void *)(ctx->clock_dev),
           (int32_t)host.slot, host.max_freq_khz,
           (unsigned)ctx->cfg.cs_setup_delay_us,
           (unsigned)ctx->cfg.cs_hold_delay_us);
  return err;
}

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device,
                                  spi_host_device_t spi_host, bool free_bus) {
  bool bus_owned = false;
  spi_device_handle_t clock_dev = NULL;
  sdspi_dev_handle_t device_handle = device;
  bool took_ctx_lock = false;
  spi_host_device_t host_id = -1;

  sdspi_ioext_context_t ctx_snapshot = {0};

  if (sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
    sdspi_ioext_context_t *ctx = NULL;

    if ((int)device_handle >= 0) {
      for (size_t i = 0; i < SDSPI_IOEXT_MAX_CTX; ++i) {
        sdspi_ioext_context_t *candidate = &s_ctx_table[i];
        if (candidate->in_use && candidate->device == device_handle) {
          ctx = candidate;
          break;
        }
      }
    }

    if (!ctx && spi_host >= 0 && spi_host < SDSPI_IOEXT_MAX_CTX) {
      sdspi_ioext_context_t *candidate = &s_ctx_table[spi_host];
      if (candidate->in_use && candidate->host_id == spi_host) {
        ctx = candidate;
      }
    }

    if (ctx) {
      ctx_snapshot = *ctx;
      ctx->in_use = false;
      memset(ctx, 0, sizeof(*ctx));
    }
    sdspi_ioext_unlock_ctx_array();
  }

  if (ctx_snapshot.lock) {
    xSemaphoreTake(ctx_snapshot.lock, portMAX_DELAY);
    took_ctx_lock = true;
  }

  bus_owned = ctx_snapshot.bus_initialized;
  clock_dev = ctx_snapshot.clock_dev;
  host_id = ctx_snapshot.host_id;
  if ((int)device_handle < 0) {
    device_handle = ctx_snapshot.device;
  }

  if (clock_dev) {
    spi_bus_remove_device(clock_dev);
  }

  if (ctx_snapshot.lock && took_ctx_lock) {
    xSemaphoreGive(ctx_snapshot.lock);
    vSemaphoreDelete(ctx_snapshot.lock);
  }

  if ((int)device_handle >= 0) {
    sdspi_host_remove_device(device_handle);
  }
  if (free_bus && bus_owned && host_id >= 0) {
    spi_bus_free(host_id);
  }
  return ESP_OK;
}
