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
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "reptile_storage.h"
#include "esp_rom_sys.h"

static const char *TAG = "BOARD";
static const char *TAG_I2C = "BOARD_I2C";
static const char *TAG_CH422 = "CH422G";
static const char *TAG_TOUCH = "GT911";
static const char *TAG_SD = "BOARD_SD";

static esp_lcd_panel_handle_t s_lcd_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static sdmmc_card_t *s_card = NULL;
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_io_dev_mode = NULL;
static i2c_master_dev_handle_t s_io_dev_in = NULL;
static i2c_master_dev_handle_t s_io_dev_out_low = NULL;
static i2c_master_dev_handle_t s_io_dev_out_high = NULL;
static bool s_board_has_expander = false;
static uint16_t s_io_state = 0;
static sdspi_dev_handle_t s_sdspi_handle = 0;
static uint8_t s_batt_raw_empty = CONFIG_BOARD_BATTERY_RAW_EMPTY;
static uint8_t s_batt_raw_full = CONFIG_BOARD_BATTERY_RAW_FULL;
static bool s_sd_diag_hold_cs_low = false;

static void board_log_i2c_levels(const char *stage);
static esp_err_t board_i2c_recover_bus(void);

static esp_err_t board_load_battery_calibration(void)
{
    int32_t stored_empty = 0;
    int32_t stored_full = 0;

    esp_err_t err_empty = storage_nvs_get_i32("battery_raw_empty", &stored_empty);
    esp_err_t err_full = storage_nvs_get_i32("battery_raw_full", &stored_full);

    if (err_empty == ESP_OK && err_full == ESP_OK) {
        if (stored_empty < 0 || stored_empty > 255 || stored_full < 0 || stored_full > 255 || stored_full <= stored_empty) {
            ESP_LOGW(TAG, "Invalid battery calibration in NVS (empty=%ld, full=%ld), using defaults", (long)stored_empty, (long)stored_full);
            return ESP_ERR_INVALID_STATE;
        }
        s_batt_raw_empty = (uint8_t)stored_empty;
        s_batt_raw_full = (uint8_t)stored_full;
        ESP_LOGI(TAG, "Battery calibration loaded from NVS: empty=%u, full=%u", s_batt_raw_empty, s_batt_raw_full);
        return ESP_OK;
    }

    s_batt_raw_empty = CONFIG_BOARD_BATTERY_RAW_EMPTY;
    s_batt_raw_full = CONFIG_BOARD_BATTERY_RAW_FULL;
    ESP_LOGI(TAG, "Battery calibration defaults applied: empty=%u, full=%u", s_batt_raw_empty, s_batt_raw_full);
    return ESP_ERR_NOT_FOUND;
}

static void board_delay_for_sd(void)
{
    // Give the card/expander some time to settle before toggling CS
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void board_log_i2c_levels(const char *stage)
{
    int sda = gpio_get_level(BOARD_I2C_SDA);
    int scl = gpio_get_level(BOARD_I2C_SCL);
    ESP_LOGI(TAG_I2C, "I2C lines (%s): SDA=%d SCL=%d", stage, sda, scl);
}

static esp_err_t board_i2c_recover_bus(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(BOARD_I2C_SDA) | BIT64(BOARD_I2C_SCL),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    board_log_i2c_levels("pre-recovery");

    int sda_level = gpio_get_level(BOARD_I2C_SDA);
    int scl_level = gpio_get_level(BOARD_I2C_SCL);
    if (sda_level == 1 && scl_level == 1) {
        ESP_LOGI(TAG_I2C, "I2C bus already idle");
        return ESP_OK;
    }

    ESP_LOGW(TAG_I2C, "Recovering I2C bus (SDA=%d SCL=%d stuck)", sda_level, scl_level);

    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    gpio_config(&cfg);

    // Pulse SCL to release potential slaves holding SDA
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(BOARD_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(BOARD_I2C_SCL, 1);
        esp_rom_delay_us(5);
        if (gpio_get_level(BOARD_I2C_SDA) == 1 && gpio_get_level(BOARD_I2C_SCL) == 1) {
            break;
        }
    }

    // Send a STOP condition (SDA rising while SCL high)
    gpio_set_level(BOARD_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(BOARD_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(BOARD_I2C_SDA, 1);
    esp_rom_delay_us(5);

    cfg.mode = GPIO_MODE_INPUT;
    gpio_config(&cfg);

    board_log_i2c_levels("post-recovery");

    sda_level = gpio_get_level(BOARD_I2C_SDA);
    scl_level = gpio_get_level(BOARD_I2C_SCL);
    if (sda_level == 1 && scl_level == 1) {
        ESP_LOGI(TAG_I2C, "I2C bus released");
        return ESP_OK;
    }

    ESP_LOGE(TAG_I2C, "I2C bus still stuck after recovery (SDA=%d SCL=%d)", sda_level, scl_level);
    return ESP_FAIL;
}

static esp_err_t io_expander_sd_cs(bool assert);
static void board_i2c_scan(void);
static esp_err_t gt911_detect_i2c_addr(uint8_t *addr_out);

static esp_err_t sdspi_transaction_with_expander(sdspi_dev_handle_t handle, sdmmc_command_t *cmd)
{
    if (!s_board_has_expander) {
        ESP_LOGE(TAG_SD, "SD transaction requested but IO expander unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_sd_diag_hold_cs_low) {
        io_expander_sd_cs(true);
        return sdspi_host_do_transaction(handle, cmd);
    }

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

#define CH422_ADDR_MODE      0x24
#define CH422_ADDR_INPUT     0x26
#define CH422_ADDR_OUT_LOW   0x38
#define CH422_ADDR_OUT_HIGH  0x23

// =============================================================================
// I2C & IO Expander
// =============================================================================

static esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus_handle) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_i2c_recover_bus(), TAG_I2C, "I2C bus recovery failed");

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_LOGI(TAG_I2C, "Initializing I2C bus (SDA=%d SCL=%d @ %d Hz)...", BOARD_I2C_SDA, BOARD_I2C_SCL,
             BOARD_I2C_FREQ_HZ);
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus_handle), TAG_I2C, "I2C bus create failed");
    board_i2c_scan();
    return ESP_OK;
}

static void board_i2c_scan(void)
{
    if (!s_i2c_bus_handle) {
        ESP_LOGW(TAG_I2C, "I2C scan skipped: bus handle not ready");
        return;
    }

    const uint8_t probes[] = {CH422_ADDR_MODE, CH422_ADDR_INPUT, CH422_ADDR_OUT_LOW, CH422_ADDR_OUT_HIGH, 0x5D, 0x14};
    int found = 0;
    int missing = 0;

    ESP_LOGI(TAG_I2C, "Probing expected I2C devices (timeout 200ms)...");
    for (size_t i = 0; i < sizeof(probes); ++i) {
        uint8_t addr = probes[i];
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, addr, pdMS_TO_TICKS(200));
        if (err == ESP_OK) {
            ++found;
            ESP_LOGI(TAG_I2C, "ACK 0x%02X", addr);
        } else {
            ++missing;
            ESP_LOGW(TAG_I2C, "No response at 0x%02X (%s)", addr, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG_I2C, "I2C probe summary: found=%d missing=%d", found, missing);
}

static esp_err_t ch422_add_device(uint8_t address, const char *label, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t dev_cfg = {
        .device_address = address,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus_handle, &dev_cfg, out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CH422, "%s add failed (0x%02X): %s", label, address, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t ch422_write_byte(i2c_master_dev_handle_t dev, uint8_t value, const char *label)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_STATE, TAG_CH422, "%s handle not ready", label);
    esp_err_t err = i2c_master_transmit(dev, &value, 1, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CH422, "%s write failed: %s", label, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t ch422_read_byte(i2c_master_dev_handle_t dev, uint8_t *value, const char *label)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_STATE, TAG_CH422, "%s handle not ready", label);
    esp_err_t err = i2c_master_receive(dev, value, 1, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CH422, "%s read failed: %s", label, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t io_expander_apply_outputs(void)
{
    uint8_t low = (uint8_t)(s_io_state & 0xFF);
    uint8_t high = (uint8_t)((s_io_state >> 8) & 0x0F);

    ESP_RETURN_ON_ERROR(ch422_write_byte(s_io_dev_out_low, low, "CH422 OUT0-7"), TAG_CH422, "Write OUT0-7 failed");
    return ch422_write_byte(s_io_dev_out_high, (uint8_t)(high | 0xF0), "CH422 OUT8-11");
}

static esp_err_t io_expander_set_output(uint8_t pin, bool level)
{
    if (pin >= 12) {
        ESP_LOGE(TAG_CH422, "Invalid CH422 pin %u", pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (level) {
        s_io_state |= (1U << pin);
    } else {
        s_io_state &= ~(1U << pin);
    }
    return io_expander_apply_outputs();
}

static esp_err_t io_expander_set_pwm(uint8_t duty)
{
    // CH422G does not expose a dedicated PWM data register on the Waveshare design.
    // Keep the API for compatibility and log for traceability.
    static bool s_warned = false;
    if (!s_warned) {
        ESP_LOGW(TAG_CH422, "PWM request duty=%u ignored (not supported by current mapping)", duty);
        s_warned = true;
    }
    return ESP_OK;
}

static esp_err_t io_expander_sd_cs(bool assert)
{
    // Active-low CS: pull low to select
    return io_expander_set_output(IO_EXP_PIN_SD_CS, !assert);
}

static esp_err_t board_io_expander_init(void)
{
    ESP_LOGI(TAG_CH422, "Initializing IO Expander (CH422G)...");
    s_board_has_expander = false;

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG_CH422, "I2C bus not ready for IO expander");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ch422_add_device(CH422_ADDR_MODE, "CH422 MODE", &s_io_dev_mode), TAG_CH422,
                        "CH422 MODE add failed");
    ESP_RETURN_ON_ERROR(ch422_add_device(CH422_ADDR_INPUT, "CH422 IN", &s_io_dev_in), TAG_CH422,
                        "CH422 IN add failed");
    ESP_RETURN_ON_ERROR(ch422_add_device(CH422_ADDR_OUT_LOW, "CH422 OUT0-7", &s_io_dev_out_low), TAG_CH422,
                        "CH422 OUT0-7 add failed");
    ESP_RETURN_ON_ERROR(ch422_add_device(CH422_ADDR_OUT_HIGH, "CH422 OUT8-11", &s_io_dev_out_high), TAG_CH422,
                        "CH422 OUT8-11 add failed");

    // Configure direction: IO0-IO6 outputs, IO7 left as input (battery sense)
    ESP_RETURN_ON_ERROR(ch422_write_byte(s_io_dev_mode, 0x7F, "CH422 MODE"), TAG_CH422,
                        "CH422 direction config failed");

    // Default outputs high for inactive CS / released resets / power enables
    s_io_state = 0;
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_VDD, true), TAG_CH422, "CH422 LCD_VDD_EN set failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_BK, true), TAG_CH422, "CH422 BK_EN set failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true), TAG_CH422, "CH422 TP_RST set failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_SD_CS, true), TAG_CH422, "CH422 SD_CS set failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_CAN_USB, false), TAG_CH422, "CH422 CAN/USB mux set failed");
    ESP_RETURN_ON_ERROR(io_expander_apply_outputs(), TAG_CH422, "CH422 outputs apply failed");
    ESP_LOGI(TAG_CH422, "EXIO configured: LCD_VDD_EN=1 BK_EN=1 TP_RST=1 SD_CS=1 (Waveshare 7B)");

    // Allow panel power to settle before reset sequence
    vTaskDelay(pdMS_TO_TICKS(10));

    // Hold LCD reset low, then release
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, false), TAG_CH422, "CH422 LCD_RST low failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, true), TAG_CH422, "CH422 LCD_RST high failed");

    // Turn on backlight fully via digital enable
    ESP_RETURN_ON_ERROR(io_expander_set_pwm(0xFF), TAG_CH422, "CH422 PWM init failed");

    s_board_has_expander = true;
    return ESP_OK;
}

// =============================================================================
// LCD RGB
// =============================================================================

static esp_err_t board_lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LCD Panel %ux%u @ %.2f MHz (Waveshare 7B)", BOARD_LCD_H_RES, BOARD_LCD_V_RES,
             (double)BOARD_LCD_PIXEL_CLOCK_HZ / 1e6);
    ESP_LOGI(TAG,
             "LCD timing (hsync bp/fp/pw=%u/%u/%u, vsync bp/fp/pw=%u/%u/%u) disp=%d pclk=%d vsync=%d hsync=%d de=%d",
             152, 48, 162,
             13, 3, 45,
             BOARD_LCD_DISP, BOARD_LCD_PCLK, BOARD_LCD_VSYNC, BOARD_LCD_HSYNC, BOARD_LCD_DE);
    ESP_LOGI(TAG,
             "LCD data pins: R0-4=%d,%d,%d,%d,%d G0-5=%d,%d,%d,%d,%d,%d B0-4=%d,%d,%d,%d,%d",
             BOARD_LCD_DATA_R0, BOARD_LCD_DATA_R1, BOARD_LCD_DATA_R2, BOARD_LCD_DATA_R3, BOARD_LCD_DATA_R4,
             BOARD_LCD_DATA_G0, BOARD_LCD_DATA_G1, BOARD_LCD_DATA_G2, BOARD_LCD_DATA_G3, BOARD_LCD_DATA_G4, BOARD_LCD_DATA_G5,
             BOARD_LCD_DATA_B0, BOARD_LCD_DATA_B1, BOARD_LCD_DATA_B2, BOARD_LCD_DATA_B3, BOARD_LCD_DATA_B4);

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
    ESP_LOGI(TAG_TOUCH, "Initializing Touch (GT911)...");

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG_TOUCH, "I2C bus not ready for touch");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_board_has_expander) {
        ESP_LOGW(TAG_TOUCH, "Skipping touch init: IO expander unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t gt911_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    ESP_RETURN_ON_ERROR(gt911_detect_i2c_addr(&gt911_addr), TAG_TOUCH, "GT911 detection failed");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_conf = {
        .dev_addr = gt911_addr,
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

    esp_err_t err = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &s_touch_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_TOUCH, "GT911 init OK on I2C addr 0x%02X, INT=%d", gt911_addr, BOARD_TOUCH_INT);
        esp_lcd_touch_read_data(s_touch_handle);
        uint16_t sample_x[1] = {0};
        uint16_t sample_y[1] = {0};
        uint8_t sample_cnt = 0;
        bool pressed = esp_lcd_touch_get_coordinates(s_touch_handle, sample_x, sample_y, NULL, &sample_cnt, 1);
        ESP_LOGI(TAG_TOUCH, "GT911 sample: pressed=%d count=%u x=%u y=%u", pressed, sample_cnt, sample_x[0], sample_y[0]);
    }
    return err;
}

static esp_err_t gt911_detect_i2c_addr(uint8_t *addr_out)
{
    ESP_RETURN_ON_FALSE(addr_out, ESP_ERR_INVALID_ARG, TAG_TOUCH, "GT911 addr_out is NULL");

    if (!s_board_has_expander) {
        ESP_LOGE(TAG_TOUCH, "GT911 detection requires IO expander (TP_RST via EXIO1)");
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t candidates[] = {0x5D, 0x14};
    bool detected = false;

    for (int attempt = 0; attempt < 2 && !detected; ++attempt) {
        bool int_level = (attempt == 0);
        gpio_config_t int_cfg = {
            .pin_bit_mask = BIT64(BOARD_TOUCH_INT),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG_TOUCH, "GT911 INT config failed (attempt %d)", attempt + 1);
        ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_TOUCH_INT, int_level), TAG_TOUCH, "GT911 INT drive failed");

        ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, false), TAG_TOUCH, "GT911 reset low failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true), TAG_TOUCH, "GT911 reset high failed");
        vTaskDelay(pdMS_TO_TICKS(80));

        ESP_LOGI(TAG_TOUCH, "GT911 detect attempt %d: TP_IRQ=%d, probing 0x5D/0x14", attempt + 1, int_level);
        for (size_t i = 0; i < sizeof(candidates); ++i) {
            uint8_t addr = candidates[i];
            esp_err_t probe = i2c_master_probe(s_i2c_bus_handle, addr, pdMS_TO_TICKS(200));
            if (probe == ESP_OK) {
                *addr_out = addr;
                detected = true;
                ESP_LOGI(TAG_TOUCH, "GT911 acknowledged at 0x%02X with TP_IRQ=%d", addr, int_level);
                break;
            }
        }
    }

    gpio_config_t float_cfg = {
        .pin_bit_mask = BIT64(BOARD_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&float_cfg), TAG_TOUCH, "GT911 INT restore to input failed");

    if (!detected) {
        ESP_LOGE(TAG_TOUCH, "GT911 not found at 0x5D or 0x14 after both INT polarities");
        return ESP_FAIL;
    }

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

#if CONFIG_BOARD_SD_HOLD_CS_LOW_DIAG
    s_sd_diag_hold_cs_low = true;
#else
    s_sd_diag_hold_cs_low = false;
#endif

    if (!s_board_has_expander) {
        ESP_LOGW(TAG_SD, "Skipping SD mount: IO expander unavailable for CS control");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_sd_diag_hold_cs_low) {
        ESP_LOGW(TAG_SD, "SD diagnostic: forcing EXIO4 (SD_CS) low for SDSPI init");
        io_expander_sd_cs(true);
    } else {
        ESP_LOGI(TAG_SD, "SD CS diagnostic pulse (low->high)");
        io_expander_sd_cs(true);
        board_delay_for_sd();
        io_expander_sd_cs(false);
        board_delay_for_sd();
    }

    ESP_LOGI(TAG_SD, "Initializing SD Card via SPI (Waveshare 7B, CS via CH422G)...");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 400; // start slow, will be increased once stable

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
        ESP_LOGE(TAG_SD, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Attach SD device to SPI bus without hardware CS (handled by expander)
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = BOARD_SD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    ret = sdspi_host_init_device(&slot_config, &s_sdspi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "Failed to init SDSPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    host.slot = s_sdspi_handle;
    host.do_transaction = sdspi_transaction_with_expander;

    const char mount_point[] = "/sdcard";

    // Ensure CS is in the expected idle state before bus activity
    if (s_sd_diag_hold_cs_low) {
        io_expander_sd_cs(true);
    } else {
        io_expander_sd_cs(false);
    }
    board_delay_for_sd();

    // Retry a few times with progressive frequency to cope with slow cards
    static const int freq_table_khz[] = {400, 1000, 5000, 20000};
    const size_t max_attempts = sizeof(freq_table_khz) / sizeof(freq_table_khz[0]);

    for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
        host.max_freq_khz = freq_table_khz[attempt];
        ESP_LOGI(TAG_SD, "SD attempt %d/%d @ %dkHz (MISO=%d MOSI=%d SCLK=%d CS=EXIO%u)",
                 (int)(attempt + 1), (int)max_attempts, host.max_freq_khz,
                 BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CLK, IO_EXP_PIN_SD_CS);

        // Toggle CS high between attempts (unless held low for diagnostics)
        if (!s_sd_diag_hold_cs_low) {
            io_expander_sd_cs(false);
            board_delay_for_sd();
        }

        ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &s_card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_SD, "SD Card mounted at %s", mount_point);
            sdmmc_card_print_info(stdout, s_card);
            return ESP_OK;
        }

        ESP_LOGW(TAG_SD, "SD mount failed (attempt %d): %s", attempt + 1, esp_err_to_name(ret));
    }

    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG_SD, "Failed to mount filesystem.");
    } else {
        ESP_LOGE(TAG_SD, "Failed to initialize the card (%s).", esp_err_to_name(ret));
    }
    ESP_LOGW(TAG_SD, "SD disabled, continuing without /sdcard (insert card and retry later)");
    return ret;
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t board_init(void)
{
    ESP_ERROR_CHECK(board_i2c_init());
    esp_err_t expander_err = board_io_expander_init();
    if (expander_err != ESP_OK) {
        s_board_has_expander = false;
        ESP_LOGE(TAG_CH422, "IO expander init failed: %s (continuing without EXIO)", esp_err_to_name(expander_err));
    }
    ESP_ERROR_CHECK(board_lcd_init());

    esp_err_t touch_err = board_touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG_TOUCH, "Touch init failed: %s (touch disabled)", esp_err_to_name(touch_err));
        s_touch_handle = NULL;
    }

    esp_err_t sd_err = board_mount_sdcard();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG_SD, "SD card not mounted: %s", esp_err_to_name(sd_err));
    }

    esp_err_t calib_err = board_load_battery_calibration();
    if (calib_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery calibration defaults in use (%u-%u) (err=%s)",
                 (unsigned)s_batt_raw_empty, (unsigned)s_batt_raw_full, esp_err_to_name(calib_err));
    }

    uint8_t batt_pct = 0;
    uint8_t batt_raw = 0;
    esp_err_t batt_err = board_get_battery_level(&batt_pct, &batt_raw);
    if (batt_err == ESP_OK) {
        ESP_LOGI(TAG, "Battery raw=%u, empty=%u, full=%u -> %u%%", batt_raw,
                 (unsigned)s_batt_raw_empty,
                 (unsigned)s_batt_raw_full,
                 batt_pct);
    } else {
        ESP_LOGW(TAG, "Battery read failed: %s", esp_err_to_name(batt_err));
    }

    return ESP_OK;
}

esp_err_t board_set_backlight_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (!s_board_has_expander) {
        return ESP_ERR_INVALID_STATE;
    }
    // Cap to ~97% to mirror Waveshare firmware safeguard
    uint8_t duty = (uint8_t)((percent * 248) / 100);
    ESP_LOGD(TAG, "Backlight percent=%u -> duty=%u", percent, duty);
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

    if (!s_board_has_expander) {
        return ESP_ERR_INVALID_STATE;
    }

    return ch422_read_byte(s_io_dev_in, inputs, "CH422 IN");
}

esp_err_t board_battery_set_calibration(uint8_t raw_empty, uint8_t raw_full)
{
    if (raw_full <= raw_empty) {
        return ESP_ERR_INVALID_ARG;
    }

    s_batt_raw_empty = raw_empty;
    s_batt_raw_full = raw_full;

    esp_err_t err_empty = storage_nvs_set_i32("battery_raw_empty", raw_empty);
    esp_err_t err_full = storage_nvs_set_i32("battery_raw_full", raw_full);
    if (err_empty != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store battery_raw_empty: %s", esp_err_to_name(err_empty));
        return err_empty;
    }
    if (err_full != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store battery_raw_full: %s", esp_err_to_name(err_full));
        return err_full;
    }

    ESP_LOGI(TAG, "Battery calibration updated: empty=%u, full=%u", s_batt_raw_empty, s_batt_raw_full);
    return ESP_OK;
}

esp_err_t board_battery_get_calibration(uint8_t *raw_empty, uint8_t *raw_full)
{
    if (raw_empty) {
        *raw_empty = s_batt_raw_empty;
    }
    if (raw_full) {
        *raw_full = s_batt_raw_full;
    }
    return ESP_OK;
}

esp_err_t board_get_battery_level(uint8_t *percent, uint8_t *raw)
{
    if (!percent) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_board_has_expander) {
        ESP_LOGW(TAG_CH422, "Battery sense unavailable: IO expander not initialized");
        return ESP_ERR_INVALID_STATE;
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
    uint32_t raw_full = s_batt_raw_full;
    uint32_t raw_empty = s_batt_raw_empty;
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
    if (!s_board_has_expander) {
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

