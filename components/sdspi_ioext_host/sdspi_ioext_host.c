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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "sdspi_ioext";
static const sdspi_dev_handle_t SDSPI_IOEXT_INVALID_DEVICE = (sdspi_dev_handle_t)-1;

// Keep the SDSPI IO-extender context bound to the canonical ESP-IDF SPI host
// count instead of a hard-coded value. SPI_HOST_MAX is provided by
// driver/spi_common.h and reflects the number of available SPI peripherals for
// the target in ESP-IDF v6.1.
#define SDSPI_IOEXT_MAX_CTX (SPI_HOST_MAX)

typedef struct {
  uint32_t cs_setup_delay_us;
  uint32_t cs_hold_delay_us;
  uint32_t max_freq_khz;
  uint8_t retries;
  uint32_t initial_clocks;
  bool cs_active_low;
  uint8_t pre_clock_bytes;
} sdspi_ioext_host_config_t;

typedef struct sdspi_ioext_context {
  sdspi_dev_handle_t device;
  spi_device_handle_t clock_dev;
  sdspi_ioext_cs_cb_t set_cs_cb;
  void *cs_user_ctx;
  sdspi_ioext_host_config_t cfg;
  bool sent_initial_clocks;
  spi_host_device_t host_id;
  bool bus_initialized;
  SemaphoreHandle_t lock;
  struct sdspi_ioext_context *next;
} sdspi_ioext_context_t;

static SemaphoreHandle_t s_ctx_lock;
static sdspi_ioext_context_t *s_ctx_head;

static bool sdspi_ioext_lock_ctx_array(TickType_t ticks_to_wait) {
  if (!s_ctx_lock) {
    s_ctx_lock = xSemaphoreCreateMutex();
    if (!s_ctx_lock) {
      ESP_LOGW(TAG, "Failed to create context list mutex");
      return false;
    }
  }
  if (xSemaphoreTake(s_ctx_lock, ticks_to_wait) != pdTRUE) {
    ESP_LOGW(TAG, "Failed to acquire context list mutex");
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
  if (!device || device == SDSPI_IOEXT_INVALID_DEVICE) {
    return NULL;
  }

  sdspi_ioext_context_t *ctx = NULL;
  if (sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
    for (sdspi_ioext_context_t *candidate = s_ctx_head; candidate;
         candidate = candidate->next) {
      if (candidate->device == device) {
        ctx = candidate;
        break;
      }
    }
    sdspi_ioext_unlock_ctx_array();
  }

  return ctx;
}

static sdspi_ioext_context_t *sdspi_ioext_alloc_ctx(void) {
  sdspi_ioext_context_t *ctx = calloc(1, sizeof(sdspi_ioext_context_t));
  if (!ctx) {
    ESP_LOGE(TAG, "Failed to allocate SDSPI IOEXT context");
    return NULL;
  }

  if (!sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
    free(ctx);
    return NULL;
  }

  ctx->next = s_ctx_head;
  s_ctx_head = ctx;
  sdspi_ioext_unlock_ctx_array();
  return ctx;
}

static sdspi_ioext_context_t *
sdspi_ioext_detach_ctx(sdspi_dev_handle_t device_handle) {
  sdspi_ioext_context_t *removed = NULL;
  if (!sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
    return NULL;
  }

  sdspi_ioext_context_t *prev = NULL;
  for (sdspi_ioext_context_t *candidate = s_ctx_head; candidate;
       candidate = candidate->next) {
    if (candidate->device == device_handle) {
      if (prev) {
        prev->next = candidate->next;
      } else {
        s_ctx_head = candidate->next;
      }
      removed = candidate;
      break;
    }
    prev = candidate;
  }

  sdspi_ioext_unlock_ctx_array();
  return removed;
}

static inline esp_err_t sdspi_ioext_toggle_cs(sdspi_ioext_context_t *ctx,
                                             bool assert) {
  if (!ctx || !ctx->set_cs_cb) {
    return ESP_ERR_INVALID_STATE;
  }

  const bool cb_assert = ctx->cfg.cs_active_low ? assert : !assert;

  esp_err_t cs_err = ctx->set_cs_cb(cb_assert, ctx->cs_user_ctx);
  if (cs_err != ESP_OK) {
    ESP_LOGW(TAG, "CS toggle %s failed: %s", assert ? "assert" : "deassert",
             esp_err_to_name(cs_err));
    return cs_err;
  }
  ESP_LOGD(TAG, "CS %s via IO expander", assert ? "ASSERT" : "DEASSERT");
  return ESP_OK;
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
  esp_err_t cs_err = sdspi_ioext_toggle_cs(ctx, false);
  if (cs_err != ESP_OK) {
    ESP_LOGW(TAG, "Proceeding with initial clocks despite CS deassert error: %s",
             esp_err_to_name(cs_err));
  }

  if (!ctx->clock_dev) {
    ESP_LOGE(TAG, "SPI clock device handle missing for host %d", ctx->host_id);
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t byte_count = ctx->cfg.pre_clock_bytes
                           ? ctx->cfg.pre_clock_bytes
                           : (ctx->cfg.initial_clocks ?
                                  (uint8_t)((ctx->cfg.initial_clocks + 7) / 8)
                                  : 10);

  if (byte_count == 0) {
    ctx->sent_initial_clocks = true;
    return ESP_OK;
  }

  uint8_t init_bytes[255];
  memset(init_bytes, 0xFF, byte_count);
  spi_transaction_t t = {
      .length = byte_count * 8U,
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
  ESP_LOGD(TAG, "Sent %u pre-clocks with CS deasserted", (unsigned)(byte_count * 8U));
  return ESP_OK;
}

static esp_err_t sdspi_ioext_do_transaction(int slot, sdmmc_command_t *cmd) {
  static bool s_first_transaction_logged = false;

  ESP_LOGD(TAG, "do_transaction entry slot=%d (0x%08x)", slot, slot);

  const sdspi_dev_handle_t h = (sdspi_dev_handle_t)slot;

  sdspi_ioext_context_t *ctx = sdspi_ioext_get_ctx_by_device(h);

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

  esp_err_t cs_err = sdspi_ioext_toggle_cs(ctx, true);
  cs_asserted = true;
  if (cs_err != ESP_OK) {
    ESP_LOGW(TAG, "Continuing despite CS assert failure: %s",
             esp_err_to_name(cs_err));
  }
  if (ctx->cfg.cs_setup_delay_us) {
    esp_rom_delay_us(ctx->cfg.cs_setup_delay_us);
  }

  ret = sdspi_host_do_transaction(h, cmd);

cleanup:
  if (ctx && ctx->cfg.cs_hold_delay_us && cs_asserted) {
    esp_rom_delay_us(ctx->cfg.cs_hold_delay_us);
  }
  if (ctx && cs_asserted) {
    cs_err = sdspi_ioext_toggle_cs(ctx, false);
    if (cs_err != ESP_OK) {
      ESP_LOGW(TAG, "CS deassert failed after transaction: %s",
               esp_err_to_name(cs_err));
    }
  }
  if (ctx->lock) {
    xSemaphoreGive(ctx->lock);
  }

  if (ret != ESP_OK) {
    ESP_LOGW(TAG,
             "SPI transaction failed (host=%d cmd=%d freq=%u kHz handle=0x%08x): %s",
             ctx ? ctx->host_id : -1, cmd ? cmd->opcode : -1,
             ctx ? (unsigned)(ctx->cfg.max_freq_khz) : 0,
             (unsigned)slot, esp_err_to_name(ret));
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

  ESP_RETURN_ON_FALSE(host_id >= SPI1_HOST && host_id < SDSPI_IOEXT_MAX_CTX,
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

  ctx = sdspi_ioext_alloc_ctx();
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
  ctx->cfg.pre_clock_bytes =
      config->pre_clock_bytes ? config->pre_clock_bytes : 10;
  ctx->cfg.initial_clocks = config->initial_clocks
                                 ? config->initial_clocks
                                 : (ctx->cfg.pre_clock_bytes * 8U);
  ctx->cfg.max_freq_khz = config->max_freq_khz;
  const bool cs_active_low_cfg =
      (config->cs_active_low || config->pre_clock_bytes) ? config->cs_active_low
                                                          : true;
  ctx->cfg.cs_active_low = cs_active_low_cfg;
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
           "sdspi_ioext: init done host=%d device_handle=%d (0x%08x) clock_dev=%p "
           "host.slot=%" PRIi32 " freq=%ukHz cs_setup=%uus cs_hold=%uus cs_active_%s",
           host_id, (int)device_handle, (unsigned int)(uintptr_t)device_handle,
           (void *)(ctx->clock_dev), (int32_t)host.slot, host.max_freq_khz,
           (unsigned)ctx->cfg.cs_setup_delay_us,
           (unsigned)ctx->cfg.cs_hold_delay_us,
           ctx->cfg.cs_active_low ? "low" : "high");
  return err;
}

esp_err_t sdspi_ioext_host_deinit(sdspi_dev_handle_t device,
                                  spi_host_device_t spi_host, bool free_bus) {
  spi_device_handle_t clock_dev = NULL;
  sdspi_dev_handle_t device_handle = device;
  bool took_ctx_lock = false;
  spi_host_device_t host_id = -1;

  sdspi_ioext_context_t *ctx_snapshot = NULL;

  if ((int)device_handle >= 0) {
    ctx_snapshot = sdspi_ioext_detach_ctx(device_handle);
  }

  if (!ctx_snapshot && spi_host >= 0) {
    // Fallback by host id if the device handle is unavailable
    sdspi_dev_handle_t found_device = SDSPI_IOEXT_INVALID_DEVICE;
    if (sdspi_ioext_lock_ctx_array(portMAX_DELAY)) {
      for (sdspi_ioext_context_t *candidate = s_ctx_head; candidate;
           candidate = candidate->next) {
        if (candidate->host_id == spi_host) {
          found_device = candidate->device;
          break;
        }
      }
      sdspi_ioext_unlock_ctx_array();
    }
    if ((int)found_device >= 0) {
      ctx_snapshot = sdspi_ioext_detach_ctx(found_device);
    }
  }

  if (ctx_snapshot && ctx_snapshot->lock) {
    xSemaphoreTake(ctx_snapshot->lock, portMAX_DELAY);
    took_ctx_lock = true;
  }

  if (ctx_snapshot) {
    clock_dev = ctx_snapshot->clock_dev;
    host_id = ctx_snapshot->host_id;
    if ((int)device_handle < 0) {
      device_handle = ctx_snapshot->device;
    }
  }

  if (clock_dev) {
    spi_bus_remove_device(clock_dev);
  }

  if (ctx_snapshot && ctx_snapshot->lock && took_ctx_lock) {
    xSemaphoreGive(ctx_snapshot->lock);
    vSemaphoreDelete(ctx_snapshot->lock);
  }

  if ((int)device_handle >= 0) {
    sdspi_host_remove_device(device_handle);
  }
  if (free_bus && host_id >= 0) {
    spi_bus_free(host_id);
  }

  if (ctx_snapshot) {
    free(ctx_snapshot);
  }
  return ESP_OK;
}
