#include "ch422g.h"
#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CH422G_REG_SYSTEM      0x00
#define CH422G_REG_OUT_HIGH    0x23
#define CH422G_REG_INPUT       0x26
#define CH422G_REG_OUT_LOW     0x38

#define CH422G_OUTPUT_PINS     16
#define CH422G_RETRY_COUNT     3

struct ch422g {
    i2c_master_dev_handle_t dev;
    uint16_t output_cache;
    uint8_t address;
};

static const char *TAG = "ch422g";

static esp_err_t ch422g_tx(ch422g_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= CH422G_RETRY_COUNT; ++attempt) {
        err = i2c_master_transmit(handle->dev, payload, sizeof(payload), pdMS_TO_TICKS(200));
        if (err == ESP_OK) {
            break;
        }
        if (attempt < CH422G_RETRY_COUNT) {
            ESP_LOGW(TAG, "tx reg 0x%02X failed (%d/%d): %s", reg, attempt, CH422G_RETRY_COUNT, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10 * attempt));
        }
    }
    return err;
}

static esp_err_t ch422g_rx(ch422g_handle_t handle, uint8_t reg, uint8_t *value_out)
{
    uint8_t cmd = reg;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= CH422G_RETRY_COUNT; ++attempt) {
        err = i2c_master_transmit_receive(handle->dev, &cmd, 1, value_out, 1, pdMS_TO_TICKS(200));
        if (err == ESP_OK) {
            break;
        }
        if (attempt < CH422G_RETRY_COUNT) {
            ESP_LOGW(TAG, "rx reg 0x%02X failed (%d/%d): %s", reg, attempt, CH422G_RETRY_COUNT, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10 * attempt));
        }
    }
    return err;
}

esp_err_t ch422g_write_reg(ch422g_handle_t handle, uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    return ch422g_tx(handle, reg, value);
}

esp_err_t ch422g_read_reg(ch422g_handle_t handle, uint8_t reg, uint8_t *value_out)
{
    ESP_RETURN_ON_FALSE(handle && value_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    return ch422g_rx(handle, reg, value_out);
}

esp_err_t ch422g_init(const ch422g_config_t *config, ch422g_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config && out_handle, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->bus, ESP_ERR_INVALID_ARG, TAG, "missing I2C bus");

    ch422g_handle_t handle = calloc(1, sizeof(struct ch422g));
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_NO_MEM, TAG, "alloc failed");

    uint8_t addr = config->address ? config->address : 0x24;
    uint32_t freq = config->scl_speed_hz ? config->scl_speed_hz : 100000;

    i2c_device_config_t dev_cfg = {
        .device_address = addr,
        .scl_speed_hz = freq,
    };

    esp_err_t err = i2c_master_bus_add_device(config->bus, &dev_cfg, &handle->dev);
    if (err != ESP_OK) {
        free(handle);
        ESP_LOGE(TAG, "bus add failed @0x%02X: %s", addr, esp_err_to_name(err));
        return err;
    }

    handle->address = addr;
    handle->output_cache = 0x0000;

    uint8_t sys_param = 0x7F;
    err = ch422g_tx(handle, CH422G_REG_SYSTEM, sys_param);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SYS_PARAM write failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(handle->dev);
        free(handle);
        return err;
    }

    err = ch422g_set_outputs(handle, handle->output_cache);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(handle->dev);
        free(handle);
        return err;
    }

    *out_handle = handle;
    ESP_LOGI(TAG, "initialized at 0x%02X (freq=%lu)", (unsigned)addr, (unsigned long)freq);
    return ESP_OK;
}

esp_err_t ch422g_deinit(ch422g_handle_t handle)
{
    if (!handle) {
        return ESP_OK;
    }
    if (handle->dev) {
        i2c_master_bus_rm_device(handle->dev);
    }
    free(handle);
    return ESP_OK;
}

esp_err_t ch422g_set_outputs(ch422g_handle_t handle, uint16_t mask)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    uint8_t low = (uint8_t)(mask & 0xFF);
    uint8_t high = (uint8_t)((mask >> 8) & 0xFF);
    ESP_RETURN_ON_ERROR(ch422g_tx(handle, CH422G_REG_OUT_LOW, low), TAG, "write OUT_LOW failed");
    ESP_RETURN_ON_ERROR(ch422g_tx(handle, CH422G_REG_OUT_HIGH, high), TAG, "write OUT_HIGH failed");
    handle->output_cache = mask;
    return ESP_OK;
}

esp_err_t ch422g_set_output_level(ch422g_handle_t handle, uint8_t pin, bool level)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    ESP_RETURN_ON_FALSE(pin < CH422G_OUTPUT_PINS, ESP_ERR_INVALID_ARG, TAG, "pin %u out of range", (unsigned)pin);

    uint16_t next = handle->output_cache;
    if (level) {
        next |= (1U << pin);
    } else {
        next &= (uint16_t)~(1U << pin);
    }
    return ch422g_set_outputs(handle, next);
}

esp_err_t ch422g_read_inputs(ch422g_handle_t handle, uint8_t *inputs_out)
{
    ESP_RETURN_ON_FALSE(handle && inputs_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    return ch422g_rx(handle, CH422G_REG_INPUT, inputs_out);
}

uint16_t ch422g_get_cached_outputs(ch422g_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    return handle->output_cache;
}

