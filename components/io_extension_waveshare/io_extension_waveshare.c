#include "io_extension_waveshare.h"
#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define IO_EXTENSION_ADDR_DEFAULT 0x24
#define IO_EXTENSION_MODE 0x02
#define IO_EXTENSION_IO_OUTPUT_ADDR 0x03
#define IO_EXTENSION_IO_INPUT_ADDR 0x04
#define IO_EXTENSION_PWM_ADDR 0x05
#define IO_EXTENSION_ADC_ADDR 0x06

#define IO_EXTENSION_MAX_PIN 7
#define IO_EXTENSION_MAX_RETRIES 3

#define IO_EXTENSION_I2C_TIMEOUT_MS 80

struct io_extension_ws {
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus;
    uint8_t last_io_value;
    SemaphoreHandle_t lock;
    uint8_t retries;
};

static const char *TAG = "io_ext_ws";

static esp_err_t io_extension_ws_recover_bus(io_extension_ws_handle_t handle)
{
    if (!handle || !handle->bus) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_master_bus_reset(handle->bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus reset failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "I2C bus reset after invalid response");
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}

static esp_err_t io_extension_ws_read_reg(io_extension_ws_handle_t handle, uint8_t reg, uint8_t *value)
{
    if (!handle || !handle->dev) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t attempts = handle->retries ? handle->retries : IO_EXTENSION_MAX_RETRIES;
    esp_err_t last_err = ESP_FAIL;
    bool attempted_recover = false;
    for (uint8_t i = 0; i < attempts; ++i) {
        last_err = i2c_master_transmit_receive(handle->dev, &reg, 1, value, 1, pdMS_TO_TICKS(IO_EXTENSION_I2C_TIMEOUT_MS));
        if (last_err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "read reg 0x%02X attempt %u/%u failed: %s", reg, (unsigned)(i + 1), (unsigned)attempts,
                 esp_err_to_name(last_err));
        if (last_err == ESP_ERR_INVALID_RESPONSE && !attempted_recover) {
            attempted_recover = true;
            io_extension_ws_recover_bus(handle);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return last_err;
}

static esp_err_t io_extension_ws_write(io_extension_ws_handle_t handle, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    const uint8_t attempts = handle->retries ? handle->retries : IO_EXTENSION_MAX_RETRIES;
    esp_err_t last_err = ESP_FAIL;
    bool attempted_recover = false;

    for (uint8_t i = 0; i < attempts; ++i) {
        last_err = i2c_master_transmit(handle->dev, payload, sizeof(payload), pdMS_TO_TICKS(IO_EXTENSION_I2C_TIMEOUT_MS));
        if (last_err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "write reg 0x%02X attempt %u/%u failed: %s", reg, (unsigned)(i + 1), (unsigned)attempts,
                 esp_err_to_name(last_err));
        if (last_err == ESP_ERR_INVALID_RESPONSE && !attempted_recover) {
            attempted_recover = true;
            io_extension_ws_recover_bus(handle);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (last_err != ESP_OK) {
        ESP_LOGE(TAG, "write reg 0x%02X=0x%02X failed after %u tries: %s", reg, value, (unsigned)attempts,
                 esp_err_to_name(last_err));
        return last_err;
    }

    uint8_t readback = 0;
    esp_err_t rb_err = io_extension_ws_read_reg(handle, reg, &readback);
    if (rb_err == ESP_OK) {
        if (readback != value) {
            // re-try a second verification before warning to avoid false positives
            vTaskDelay(pdMS_TO_TICKS(2));
            rb_err = io_extension_ws_read_reg(handle, reg, &readback);
        }
    }

    if (rb_err == ESP_OK) {
        if (readback != value) {
            ESP_LOGW(TAG, "verify reg 0x%02X mismatch (wrote 0x%02X read 0x%02X)", reg, value, readback);
        }
    } else {
        ESP_LOGW(TAG, "reg 0x%02X<=0x%02X written, readback skipped: %s", reg, value, esp_err_to_name(rb_err));
    }
    return last_err;
}

esp_err_t io_extension_ws_init(const io_extension_ws_config_t *config, io_extension_ws_handle_t *handle_out)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config && handle_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    ESP_RETURN_ON_FALSE(config->bus, ESP_ERR_INVALID_ARG, TAG, "missing bus handle");

    io_extension_ws_handle_t handle = calloc(1, sizeof(struct io_extension_ws));
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_NO_MEM, TAG, "alloc failed");

    handle->lock = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(handle->lock, ESP_ERR_NO_MEM, err, TAG, "mutex alloc failed");

    uint8_t addr = config->address ? config->address : IO_EXTENSION_ADDR_DEFAULT;
    uint32_t freq = config->scl_speed_hz ? config->scl_speed_hz : 50000;
    handle->retries = (config->retries == 0) ? IO_EXTENSION_MAX_RETRIES : config->retries;
    handle->bus = config->bus;

    i2c_device_config_t dev_cfg = {
        .device_address = addr,
        .scl_speed_hz = freq,
    };

    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(config->bus, &dev_cfg, &handle->dev), err, TAG, "add device failed");

    // Configure all pins as outputs initially
    ESP_GOTO_ON_ERROR(io_extension_ws_write(handle, IO_EXTENSION_MODE, 0xFF), err, TAG, "mode write failed");

    handle->last_io_value = 0xFF;
    *handle_out = handle;
    ESP_LOGI(TAG, "initialized at 0x%02X (freq=%lu retries=%u)", (unsigned)addr, (unsigned long)freq,
             (unsigned)handle->retries);
    return ESP_OK;

err:
    if (handle) {
        if (handle->dev) {
            i2c_master_bus_rm_device(handle->dev);
        }
        if (handle->lock) {
            vSemaphoreDelete(handle->lock);
        }
        free(handle);
    }
    return ret;
}

esp_err_t io_extension_ws_set_output(io_extension_ws_handle_t handle, uint8_t pin, bool level)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    ESP_RETURN_ON_FALSE(pin <= IO_EXTENSION_MAX_PIN, ESP_ERR_INVALID_ARG, TAG, "pin %u out of range", (unsigned)pin);

    if (xSemaphoreTake(handle->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (level) {
        handle->last_io_value |= (1U << pin);
    } else {
        handle->last_io_value &= ~(1U << pin);
    }
    esp_err_t err = io_extension_ws_write(handle, IO_EXTENSION_IO_OUTPUT_ADDR, handle->last_io_value);
    xSemaphoreGive(handle->lock);
    return err;
}

esp_err_t io_extension_ws_set_pwm_raw(io_extension_ws_handle_t handle, uint8_t duty)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    if (xSemaphoreTake(handle->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = io_extension_ws_write(handle, IO_EXTENSION_PWM_ADDR, duty);
    xSemaphoreGive(handle->lock);
    return err;
}

esp_err_t io_extension_ws_set_pwm_percent(io_extension_ws_handle_t handle, uint8_t percent)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "null handle");
    if (percent > 100) {
        percent = 100;
    }

    uint8_t duty = (uint8_t)(((uint32_t)percent * 255 + 50) / 100);
    return io_extension_ws_set_pwm_raw(handle, duty);
}

esp_err_t io_extension_ws_read_outputs(io_extension_ws_handle_t handle, uint8_t *value_out)
{
    ESP_RETURN_ON_FALSE(handle && value_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    if (xSemaphoreTake(handle->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = io_extension_ws_read_reg(handle, IO_EXTENSION_IO_OUTPUT_ADDR, value_out);
    xSemaphoreGive(handle->lock);
    return err;
}

esp_err_t io_extension_ws_read_inputs(io_extension_ws_handle_t handle, uint8_t *value_out)
{
    ESP_RETURN_ON_FALSE(handle && value_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    uint8_t reg = IO_EXTENSION_IO_INPUT_ADDR;
    if (xSemaphoreTake(handle->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit_receive(handle->dev, &reg, 1, value_out, 1,
                                                pdMS_TO_TICKS(IO_EXTENSION_I2C_TIMEOUT_MS));
    xSemaphoreGive(handle->lock);
    return err;
}

esp_err_t io_extension_ws_read_adc(io_extension_ws_handle_t handle, uint16_t *value_out)
{
    ESP_RETURN_ON_FALSE(handle && value_out, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    uint8_t reg = IO_EXTENSION_ADC_ADDR;
    uint8_t buf[2] = {0};
    if (xSemaphoreTake(handle->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit_receive(handle->dev, &reg, 1, buf, sizeof(buf),
                                                pdMS_TO_TICKS(IO_EXTENSION_I2C_TIMEOUT_MS));
    if (err == ESP_OK) {
        *value_out = (uint16_t)(buf[1] << 8 | buf[0]);
    }
    xSemaphoreGive(handle->lock);
    return err;
}

esp_err_t io_extension_ws_deinit(io_extension_ws_handle_t handle)
{
    if (!handle) {
        return ESP_OK;
    }
    if (handle->dev) {
        i2c_master_bus_rm_device(handle->dev);
    }
    if (handle->lock) {
        vSemaphoreDelete(handle->lock);
    }
    free(handle);
    return ESP_OK;
}

