#include <stdbool.h>
#include <stdio.h>
#include "board.h"
#include "board_pins.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "BOARD";

static esp_lcd_panel_handle_t s_lcd_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static sdmmc_card_t *s_card = NULL;
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_io_expander_dev = NULL;
static uint8_t s_io_state = 0;
static sdspi_dev_handle_t s_sdspi_handle = 0;

static esp_err_t io_expander_sd_cs(bool assert);

static esp_err_t sdspi_transaction_with_expander(sdspi_dev_handle_t handle, sdmmc_command_t *cmd)
{
    io_expander_sd_cs(true);
    esp_err_t ret = sdspi_host_do_transaction(handle, cmd);
    io_expander_sd_cs(false);
    return ret;
}

#define IO_EXP_PIN_TOUCH_RST 1
#define IO_EXP_PIN_BK        2
#define IO_EXP_PIN_LCD_RST   3
#define IO_EXP_PIN_SD_CS     4
#define IO_EXP_PIN_CAN_USB   5
#define IO_EXP_PIN_LCD_VDD   6

// =============================================================================
// I2C & IO Expander
// =============================================================================

static esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus_handle) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_LOGI(TAG, "Initializing I2C bus...");
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_i2c_bus_handle));
    return ESP_OK;
}

static esp_err_t io_expander_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_transmit(s_io_expander_dev, payload, sizeof(payload), pdMS_TO_TICKS(100));
}

static esp_err_t io_expander_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_io_expander_dev, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static esp_err_t io_expander_apply_outputs(void)
{
    return io_expander_write_reg(0x03, s_io_state);
}

static esp_err_t io_expander_set_output(uint8_t pin, bool level)
{
    if (level) {
        s_io_state |= (1 << pin);
    } else {
        s_io_state &= ~(1 << pin);
    }
    return io_expander_apply_outputs();
}

static esp_err_t io_expander_set_pwm(uint8_t duty)
{
    uint8_t payload[2] = {0x05, duty};
    return i2c_master_transmit(s_io_expander_dev, payload, sizeof(payload), pdMS_TO_TICKS(100));
}

static esp_err_t io_expander_sd_cs(bool assert)
{
    // Active-low CS: pull low to select
    return io_expander_set_output(IO_EXP_PIN_SD_CS, !assert);
}

static esp_err_t board_io_expander_init(void)
{
    ESP_LOGI(TAG, "Initializing IO Expander (CH32V003)...");

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "I2C bus not ready for IO expander");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .device_address = BOARD_IO_EXP_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus_handle, &dev_cfg, &s_io_expander_dev));

    // Configure direction: IO0-IO6 outputs, IO7 left as input (battery sense)
    ESP_ERROR_CHECK(io_expander_write_reg(0x02, 0x7F));

    // Start with everything low for a clean state
    s_io_state = 0x00;
    ESP_ERROR_CHECK(io_expander_apply_outputs());

    // Ensure SD card is deselected and CAN/USB defaults to USB (low)
    ESP_ERROR_CHECK(io_expander_sd_cs(false));
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_SD_CS, true));
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_CAN_USB, false));

    // Enable panel power
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_LCD_VDD, true));
    vTaskDelay(pdMS_TO_TICKS(10));

    // Hold LCD reset low, then release
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_LCD_RST, false));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_LCD_RST, true));

    // Keep touch reset asserted low until touch init handles it
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, false));

    // Turn on backlight fully via digital enable
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_BK, true));
    ESP_ERROR_CHECK(io_expander_set_pwm(0xFF));

    return ESP_OK;
}

// =============================================================================
// LCD RGB
// =============================================================================

static esp_err_t board_lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LCD Panel...");

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16, // RGB565
        .num_fbs = 2,   // Double buffer in PSRAM
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = BOARD_LCD_DISP,
        .pclk_gpio_num = BOARD_LCD_PCLK,
        .vsync_gpio_num = BOARD_LCD_VSYNC,
        .hsync_gpio_num = BOARD_LCD_HSYNC,
        .de_gpio_num = BOARD_LCD_DE,
        .data_gpio_nums = {
            BOARD_LCD_DATA_B0, BOARD_LCD_DATA_B1, BOARD_LCD_DATA_B2, BOARD_LCD_DATA_B3, BOARD_LCD_DATA_B4,
            BOARD_LCD_DATA_G0, BOARD_LCD_DATA_G1, BOARD_LCD_DATA_G2, BOARD_LCD_DATA_G3, BOARD_LCD_DATA_G4, BOARD_LCD_DATA_G5,
            BOARD_LCD_DATA_R0, BOARD_LCD_DATA_R1, BOARD_LCD_DATA_R2, BOARD_LCD_DATA_R3, BOARD_LCD_DATA_R4,
        },
        .timings = {
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_back_porch = 152,
            .hsync_front_porch = 48,
            .hsync_pulse_width = 162,
            .vsync_back_porch = 13,
            .vsync_front_porch = 3,
            .vsync_pulse_width = 45,
            .flags.pclk_active_neg = true,
        },
        .flags.fb_in_psram = 1, // Allocate frame buffer in PSRAM
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &s_lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd_panel_handle));

    return ESP_OK;
}

// =============================================================================
// Touch
// =============================================================================

static esp_err_t board_touch_init(void)
{
    ESP_LOGI(TAG, "Initializing Touch (GT911)...");

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "I2C bus not ready for touch");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_config_t int_cfg = {
        .pin_bit_mask = BIT64(BOARD_TOUCH_INT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&int_cfg));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_TOUCH_INT, 1));

    // Reset GT911 through IO expander
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, false));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true));
    vTaskDelay(pdMS_TO_TICKS(100));

    // Set INT back to input for interrupt signaling
    int_cfg.mode = GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&int_cfg));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_conf = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus_handle, &io_conf, &io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &s_touch_handle));

    return ESP_OK;
}

// =============================================================================
// SD Card
// =============================================================================

esp_err_t board_mount_sdcard(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SD Card via SPI...");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_MOSI,
        .miso_io_num = BOARD_SD_MISO,
        .sclk_io_num = BOARD_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Note: If SPI bus is shared, check if it's already initialized.
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Attach SD device to SPI bus without hardware CS (handled by expander)
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = BOARD_SD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    ret = sdspi_host_init_device(&slot_config, &s_sdspi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SDSPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    host.slot = s_sdspi_handle;
    host.do_transaction = sdspi_transaction_with_expander;

    // Ensure CS is high before bus activity
    io_expander_sd_cs(false);

    const char mount_point[] = "/sdcard";
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &s_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "SD Card mounted at %s", mount_point);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t board_init(void)
{
    ESP_ERROR_CHECK(board_i2c_init());
    ESP_ERROR_CHECK(board_io_expander_init());
    ESP_ERROR_CHECK(board_lcd_init());
    ESP_ERROR_CHECK(board_touch_init());
    board_mount_sdcard();

    return ESP_OK;
}

esp_err_t board_set_backlight_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    // Cap to ~97% to mirror Waveshare firmware safeguard
    uint8_t duty = (uint8_t)((percent * 248) / 100);
    esp_err_t err = io_expander_set_pwm(duty);
    if (err != ESP_OK) {
        return err;
    }
    return io_expander_set_output(IO_EXP_PIN_BK, percent > 0);
}

esp_err_t board_io_expander_read_inputs(uint8_t *inputs)
{
    if (!inputs) {
        return ESP_ERR_INVALID_ARG;
    }
    return io_expander_read_reg(0x00, inputs);
}

esp_err_t board_get_battery_level(uint8_t *percent, uint8_t *raw)
{
    if (!percent) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t inputs = 0;
    esp_err_t err = board_io_expander_read_inputs(&inputs);
    if (err != ESP_OK) {
        return err;
    }

    if (raw) {
        *raw = inputs;
    }

    // The CH32V003 firmware exposes the battery sense as an 8-bit value on the
    // input register (IO7). Scale linearly between CONFIG_BOARD_BATTERY_RAW_EMPTY
    // and CONFIG_BOARD_BATTERY_RAW_FULL to obtain a 0-100% rounded percentage.
    uint32_t raw_full = CONFIG_BOARD_BATTERY_RAW_FULL;
    uint32_t raw_empty = CONFIG_BOARD_BATTERY_RAW_EMPTY;
    if (raw_full <= raw_empty) {
        raw_full = raw_empty + 1; // avoid divide-by-zero
    }

    int32_t adjusted = (int32_t)inputs - (int32_t)raw_empty;
    if (adjusted < 0) {
        adjusted = 0;
    }

    uint32_t span = raw_full - raw_empty;
    uint32_t scaled = ((uint32_t)adjusted * 100U + span / 2U) / span;
    if (scaled > 100U) {
        scaled = 100U;
    }

    *percent = (uint8_t)scaled;
    return ESP_OK;
}

esp_err_t board_set_can_mode(bool enable_can)
{
    if (!s_io_expander_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return io_expander_set_output(IO_EXP_PIN_CAN_USB, enable_can);
}

esp_lcd_panel_handle_t board_get_lcd_handle(void)
{
    return s_lcd_panel_handle;
}

esp_lcd_touch_handle_t board_get_touch_handle(void)
{
    return s_touch_handle;
}

