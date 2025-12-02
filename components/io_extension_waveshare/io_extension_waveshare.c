#include "io_extension_waveshare.h"
#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define IO_EXTENSION_ADDR_DEFAULT 0x24
#define IO_EXTENSION_MODE 0x02
#define IO_EXTENSION_IO_OUTPUT_ADDR 0x03
#define IO_EXTENSION_IO_INPUT_ADDR 0x04
#define IO_EXTENSION_PWM_ADDR 0x05
#define IO_EXTENSION_ADC_ADDR 0x06

#define IO_EXTENSION_MAX_PIN 7

struct io_extension_ws {
    i2c_master_dev_handle_t dev;
    uint8_t last_io_value;
};

static const char *TAG = "io_ext_ws";

static esp_err_t io_extension_ws_write(io_extension_ws_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(handle->dev, payload, sizeof(payload), pdMS_TO_TICKS(100));
}

esp_err_t io_extension_ws_init(const io_extension_ws_config_t *config, io_extension_ws_handle_t *handle_out)
{
    ESP_RETURN_ON_FALSE(config && handle_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->bus, ESP_ERR_INVALID_ARG, TAG, "missing bus handle");

    io_extension_ws_handle_t handle = calloc(1, sizeof(struct io_extension_ws));
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_NO_MEM, TAG, "alloc failed");

    uint8_t addr = config->address ? config->address : IO_EXTENSION_ADDR_DEFAULT;
    uint32_t freq = config->scl_speed_hz ? config->scl_speed_hz : 100000;

    i2c_device_config_t dev_cfg = {
        .device_address = addr,
        .scl_speed_hz = freq,
    };

    esp_err_t err = i2c_master_bus_add_device(config->bus, &dev_cfg, &handle->dev);
    if (err != ESP_OK) {
        free(handle);
        return err;
    }

    // Configure all pins as outputs initially
    err = io_extension_ws_write(handle, IO_EXTENSION_MODE, 0xFF);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(handle->dev);
        free(handle);
        ESP_LOGE(TAG, "mode write failed: %s", esp_err_to_name(err));
        return err;
    }

    handle->last_io_value = 0xFF;
    *handle_out = handle;
    ESP_LOGI(TAG, "initialized at 0x%02X (freq=%lu)", (unsigned)addr, (unsigned long)freq);
    return ESP_OK;
}

esp_err_t io_extension_ws_set_output(io_extension_ws_handle_t handle, uint8_t pin, bool level)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    ESP_RETURN_ON_FALSE(pin <= IO_EXTENSION_MAX_PIN, ESP_ERR_INVALID_ARG, TAG, "pin %u out of range", (unsigned)pin);

    if (level) {
        handle->last_io_value |= (1U << pin);
    } else {
        handle->last_io_value &= ~(1U << pin);
    }
    return io_extension_ws_write(handle, IO_EXTENSION_IO_OUTPUT_ADDR, handle->last_io_value);
}

esp_err_t io_extension_ws_set_pwm_percent(io_extension_ws_handle_t handle, uint8_t percent)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    if (percent > 100) {
        percent = 100;
    }
    if (percent > 97) {
        percent = 97; // firmware note: avoid screen fully off
    }

    uint8_t duty = (uint8_t)((percent * 255) / 100);
    return io_extension_ws_write(handle, IO_EXTENSION_PWM_ADDR, duty);
}

esp_err_t io_extension_ws_read_inputs(io_extension_ws_handle_t handle, uint8_t *value_out)
{
    ESP_RETURN_ON_FALSE(handle && value_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    uint8_t reg = IO_EXTENSION_IO_INPUT_ADDR;
    return i2c_master_transmit_receive(handle->dev, &reg, 1, value_out, 1, pdMS_TO_TICKS(100));
}

esp_err_t io_extension_ws_read_adc(io_extension_ws_handle_t handle, uint16_t *value_out)
{
    ESP_RETURN_ON_FALSE(handle && value_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    uint8_t reg = IO_EXTENSION_ADC_ADDR;
    uint8_t buf[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(handle->dev, &reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        return err;
    }
    *value_out = (uint16_t)(buf[1] << 8 | buf[0]);
    return ESP_OK;
}

esp_err_t io_extension_ws_deinit(io_extension_ws_handle_t handle)
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

