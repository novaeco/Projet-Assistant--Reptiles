#include <stdbool.h>
#include <stdio.h>
#include <string.h>
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
#include "esp_system.h"
#include "io_extension_waveshare.h"
#include "sdspi_ioext_host.h"

static const char *TAG = "BOARD";
static const char *TAG_I2C = "BOARD_I2C";
static const char *TAG_CH422 = "CH422G";
static const char *TAG_TOUCH = "GT911";
static const char *TAG_SD = "BOARD_SD";
static const char *TAG_IO = "IO_EXT";

static esp_lcd_panel_handle_t s_lcd_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static sdmmc_card_t *s_card = NULL;
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_io_dev = NULL;
static io_extension_ws_handle_t s_io_ws = NULL;
static bool s_board_has_expander = false;
static bool s_board_has_lcd = false;
static uint16_t s_io_state = 0;
static sdspi_dev_handle_t s_sdspi_handle = 0;
static uint8_t s_batt_raw_empty = CONFIG_BOARD_BATTERY_RAW_EMPTY;
static uint8_t s_batt_raw_full = CONFIG_BOARD_BATTERY_RAW_FULL;
static bool s_logged_expander_absent = false;
static bool s_logged_batt_unavailable = false;

typedef enum {
    IO_DRIVER_NONE = 0,
    IO_DRIVER_WAVESHARE,
    IO_DRIVER_CH422,
} io_driver_kind_t;

static io_driver_kind_t s_io_driver = IO_DRIVER_NONE;

typedef struct {
    bool mode_ack;
    bool input_ack;
    bool out_low_ack;
    bool out_high_ack;
    int ack_count;
    int expected_count;
} ch422_probe_result_t;

static void board_log_i2c_levels(const char *stage);
static esp_err_t board_i2c_recover_bus(void);
static esp_err_t board_i2c_selftest(void);
static esp_err_t ch422_probe(ch422_probe_result_t *result);
static void ch422_log_probe_result(const ch422_probe_result_t *result);

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
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int sda_level = gpio_get_level(BOARD_I2C_SDA);
    int scl_level = gpio_get_level(BOARD_I2C_SCL);
    board_log_i2c_levels("pre-recovery");

    if (sda_level == 0 || scl_level == 0) {
        ESP_LOGW(TAG_I2C, "I2C bus not idle before recovery (SDA=%d SCL=%d)", sda_level, scl_level);
    } else {
        ESP_LOGI(TAG_I2C, "I2C bus idle, sending recovery sequence proactively");
    }

    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&cfg);

    // Pulse SCL to release potential slaves holding SDA
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(BOARD_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(BOARD_I2C_SCL, 1);
        esp_rom_delay_us(5);
    }

    // Send a STOP condition (SDA rising while SCL high)
    gpio_set_level(BOARD_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(BOARD_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(BOARD_I2C_SDA, 1);
    esp_rom_delay_us(5);

    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
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

static esp_err_t io_expander_sd_cs(bool assert, void *ctx);
static void board_i2c_scan(void);
static esp_err_t gt911_detect_i2c_addr(uint8_t *addr_out, bool can_reset);
static bool board_touch_fetch(uint16_t *x, uint16_t *y, uint8_t *count_out);

static bool board_touch_fetch(uint16_t *x, uint16_t *y, uint8_t *count_out)
{
    if (!s_touch_handle) {
        return false;
    }

    uint8_t count = 0;
    esp_lcd_touch_read_data(s_touch_handle);

    bool pressed = false;
    esp_err_t err = ESP_OK;

    if (esp_lcd_touch_get_data) {
        err = esp_lcd_touch_get_data(s_touch_handle, x, y, NULL, &count, 1);
        pressed = (err == ESP_OK && count > 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG_TOUCH, "esp_lcd_touch_get_data failed: %s", esp_err_to_name(err));
        }
    } else {
        pressed = esp_lcd_touch_get_coordinates(s_touch_handle, x, y, NULL, &count, 1);
        err = pressed ? ESP_OK : ESP_FAIL;
    }

    if (count_out) {
        *count_out = count;
    }
    return pressed;
}

#define IO_EXP_PIN_TOUCH_RST 1
#define IO_EXP_PIN_BK        2
#define IO_EXP_PIN_LCD_RST   3
#define IO_EXP_PIN_SD_CS     4
#define IO_EXP_PIN_CAN_USB   5
#define IO_EXP_PIN_LCD_VDD   6

#define CH422_ADDR           0x24
#define CH422_CMD_SYS_PARAM  0x00  // System parameter byte (IO extension, outputs enabled)
#define CH422_CMD_IO_OUTPUT  0x70  // Write IO7..IO0 output levels
#define CH422_CMD_OC_OUTPUT  0x46  // Open-collector outputs (not used on this board)
#define CH422_CMD_IO_INPUT   0x4D  // Read IO7..IO0 inputs

// =============================================================================
// I2C & IO Expander
// =============================================================================

static esp_err_t ch422_probe(ch422_probe_result_t *result)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    const struct {
        uint8_t addr;
        bool *flag;
    } expected[] = {
        {CH422_ADDR, &result->mode_ack},
        {0x26, &result->input_ack},
        {0x38, &result->out_low_ack},
        {0x23, &result->out_high_ack},
    };

    result->expected_count = (int)(sizeof(expected) / sizeof(expected[0]));

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, expected[i].addr, pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            ++result->ack_count;
            *(expected[i].flag) = true;
        }
    }

    if (!result->mode_ack) {
        return ESP_ERR_NOT_FOUND;
    }

    if (result->ack_count < result->expected_count) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static void ch422_log_probe_result(const ch422_probe_result_t *result)
{
    if (!result) {
        return;
    }

    if (result->ack_count == 0) {
        ESP_LOGE(TAG_CH422, "CH422G not detected: no ACK on expected addresses (0x23/0x24/0x26/0x38)");
        ESP_LOGW(TAG_CH422, "Check IO expander power, I2C pull-ups, and reset wiring.");
        return;
    }

    ESP_LOGI(TAG_CH422, "CH422G probe: MODE=%s INPUT=%s OUT_LOW=%s OUT_HIGH=%s (ack %d/%d)",
             result->mode_ack ? "ACK" : "NACK",
             result->input_ack ? "ACK" : "NACK",
             result->out_low_ack ? "ACK" : "NACK",
             result->out_high_ack ? "ACK" : "NACK",
             result->ack_count, result->expected_count);

    if (!result->mode_ack) {
        ESP_LOGE(TAG_CH422, "CH422G MODE register not responding at 0x24");
    }
    if (result->mode_ack && result->ack_count < result->expected_count) {
        ESP_LOGW(TAG_CH422, "CH422G partially detected (missing registers). Output control may be unavailable.");
    }
}

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
        .flags.enable_internal_pullup = CONFIG_BOARD_I2C_ENABLE_INTERNAL_PULLUP,
    };

    ESP_LOGI(TAG_I2C, "Initializing I2C bus (SDA=%d SCL=%d @ %d Hz, internal PU=%s)...", BOARD_I2C_SDA, BOARD_I2C_SCL,
             BOARD_I2C_FREQ_HZ, CONFIG_BOARD_I2C_ENABLE_INTERNAL_PULLUP ? "on" : "off");
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus_handle), TAG_I2C, "I2C bus create failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(board_i2c_selftest(), TAG_I2C, "I2C selftest failed");
    board_i2c_scan();
    return ESP_OK;
}

static void board_i2c_scan(void)
{
    if (!s_i2c_bus_handle) {
        ESP_LOGW(TAG_I2C, "I2C scan skipped: bus handle not ready");
        return;
    }

    const uint8_t expected[] = {0x24, 0x5D, 0x14};
    int found = 0;

    esp_log_level_t prev_level = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_WARN);

    ESP_LOGI(TAG_I2C, "Probing I2C endpoints (0x24/0x5D/0x14) @ %d Hz...", BOARD_I2C_FREQ_HZ);
    for (size_t i = 0; i < sizeof(expected); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, expected[i], pdMS_TO_TICKS(60));
        if (err == ESP_OK) {
            ++found;
        }
    }
    ESP_LOGI(TAG_I2C, "I2C targeted probe complete: %d/%d responded", found, (int)(sizeof(expected)));

#ifdef CONFIG_BOARD_I2C_FULL_SCAN_DEBUG
    ESP_LOGW(TAG_I2C, "Debug full scan enabled (may log timeouts)");
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
        i2c_master_probe(s_i2c_bus_handle, addr, pdMS_TO_TICKS(10));
    }
#endif

    esp_log_level_set("i2c.master", prev_level);
}

static esp_err_t board_i2c_selftest(void)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus_handle, ESP_ERR_INVALID_STATE, TAG_I2C, "I2C handle missing for selftest");

    int sda = gpio_get_level(BOARD_I2C_SDA);
    int scl = gpio_get_level(BOARD_I2C_SCL);
    ESP_LOGI(TAG_I2C, "I2C idle check: SDA=%d SCL=%d", sda, scl);
    if (sda == 0 || scl == 0) {
        ESP_LOGW(TAG_I2C, "I2C lines not pulled high at rest (pull-ups missing or device holding bus)");
    }

    struct {
        uint8_t addr;
        const char *label;
        bool detected;
    } expected[] = {
        {0x24, "IO EXT", false},
        {0x5D, "GT911 (alt1)", false},
        {0x14, "GT911 (alt2)", false},
    };

    int ack = 0;
    esp_log_level_t prev_level = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_WARN);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, expected[i].addr, pdMS_TO_TICKS(60));
        if (err == ESP_OK) {
            ++ack;
            expected[i].detected = true;
            ESP_LOGI(TAG_I2C, "ACK 0x%02X (%s)", expected[i].addr, expected[i].label);
        }
    }

    esp_log_level_set("i2c.master", prev_level);

    if (ack == 0) {
        ESP_LOGE(TAG_I2C, "I2C selftest: no expected device responded. Check pull-ups, power rails, and wiring (SDA=%d SCL=%d)",
                 sda, scl);
#if CONFIG_BOARD_REQUIRE_I2C_OK
        esp_system_abort("CONFIG_BOARD_REQUIRE_I2C_OK enabled: I2C bus down");
#else
        ESP_LOGW(TAG_I2C, "Continuing despite I2C selftest failure (CONFIG_BOARD_REQUIRE_I2C_OK disabled)");
#endif
        return ESP_ERR_NOT_FOUND;
    }

    bool io_ok = expected[0].detected;
    bool touch_ok = expected[1].detected || expected[2].detected;
    ESP_LOGI(TAG_I2C, "I2C selftest: IO_EXT=%s, TOUCH=%s (ack %d/%d)",
             io_ok ? "yes" : "no",
             touch_ok ? "yes" : "no",
             ack, (int)(sizeof(expected) / sizeof(expected[0])));

    return ESP_OK;
}

static esp_err_t ch422_add_device(void)
{
    i2c_device_config_t dev_cfg = {
        .device_address = CH422_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus_handle, &dev_cfg, &s_io_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CH422, "CH422 add failed (0x%02X): %s", CH422_ADDR, esp_err_to_name(err));
    }
    return err;
}

static void ch422_release_handle(void)
{
    if (s_io_dev) {
        i2c_master_bus_rm_device(s_io_dev);
        s_io_dev = NULL;
    }
}

static esp_err_t ch422_send_cmd(const uint8_t *payload, size_t len, const char *label)
{
    ESP_RETURN_ON_FALSE(s_io_dev, ESP_ERR_INVALID_STATE, TAG_CH422, "%s handle not ready", label);
    const int attempts = 3;
    esp_err_t err = ESP_FAIL;

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        err = i2c_master_transmit(s_io_dev, payload, len, pdMS_TO_TICKS(100));
        if (err == ESP_OK) {
            break;
        }

        if (attempt < attempts) {
            ESP_LOGW(TAG_CH422, "%s write failed (attempt %d/%d): %s", label, attempt, attempts, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(20 * attempt));
        } else {
            ESP_LOGE(TAG_CH422, "%s write failed after %d attempts: %s", label, attempts, esp_err_to_name(err));
        }
    }
    return err;
}

static esp_err_t ch422_read_inputs(uint8_t *value)
{
    ESP_RETURN_ON_FALSE(s_io_dev, ESP_ERR_INVALID_STATE, TAG_CH422, "CH422 IN handle not ready");
    uint8_t cmd = CH422_CMD_IO_INPUT;
    const int attempts = 3;
    esp_err_t err = ESP_FAIL;

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        err = i2c_master_transmit_receive(s_io_dev, &cmd, 1, value, 1, pdMS_TO_TICKS(100));
        if (err == ESP_OK) {
            break;
        }
        if (attempt < attempts) {
            ESP_LOGW(TAG_CH422, "CH422 IO_IN read failed (attempt %d/%d): %s", attempt, attempts, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(20 * attempt));
        } else {
            ESP_LOGE(TAG_CH422, "CH422 IO_IN read failed after %d attempts: %s", attempts, esp_err_to_name(err));
        }
    }
    return err;
}

static esp_err_t io_expander_apply_outputs(void)
{
    if (s_io_driver != IO_DRIVER_CH422) {
        return ESP_OK;
    }
    uint8_t out = (uint8_t)(s_io_state & 0xFF);
    uint8_t payload[2] = {CH422_CMD_IO_OUTPUT, out};
    ESP_RETURN_ON_ERROR(ch422_send_cmd(payload, sizeof(payload), "CH422 IO_OUT"), TAG_CH422,
                        "Write IO_OUT failed");
    return ESP_OK;
}

static esp_err_t io_expander_set_output(uint8_t pin, bool level)
{
    switch (s_io_driver) {
    case IO_DRIVER_WAVESHARE:
        return io_extension_ws_set_output(s_io_ws, pin, level);
    case IO_DRIVER_CH422:
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
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

static esp_err_t io_expander_set_pwm(uint8_t duty)
{
    if (!s_board_has_expander) {
        return ESP_ERR_INVALID_STATE;
    }
    switch (s_io_driver) {
    case IO_DRIVER_WAVESHARE:
        return io_extension_ws_set_pwm_percent(s_io_ws, duty);
    case IO_DRIVER_CH422:
        return io_expander_set_output(IO_EXP_PIN_BK, duty != 0);
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

static esp_err_t io_expander_sd_cs(bool assert, void *ctx)
{
    (void)ctx;
    // Active-low CS: pull low to select
    return io_expander_set_output(IO_EXP_PIN_SD_CS, !assert);
}

static esp_err_t board_io_expander_post_init_defaults(void)
{
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_VDD, true), TAG_IO, "LCD_VDD on failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_BK, true), TAG_IO, "BK default on failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true), TAG_IO, "TP_RST default high failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_SD_CS, true), TAG_IO, "SD CS idle high failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_CAN_USB, false), TAG_IO, "CAN/USB default failed");

    if (s_io_driver == IO_DRIVER_CH422) {
        ESP_RETURN_ON_ERROR(io_expander_apply_outputs(), TAG_CH422, "apply outputs failed");
        ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, false), TAG_CH422, "LCD_RST low failed");
        vTaskDelay(pdMS_TO_TICKS(5));
        ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, true), TAG_CH422, "LCD_RST high failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(io_expander_set_pwm(100), TAG_CH422, "PWM init failed");
    } else if (s_io_driver == IO_DRIVER_WAVESHARE) {
        ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, false), TAG_IO, "LCD_RST low failed");
        vTaskDelay(pdMS_TO_TICKS(5));
        ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, true), TAG_IO, "LCD_RST high failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(io_expander_set_pwm(100), TAG_IO, "PWM init failed");
    }
    return ESP_OK;
}

static esp_err_t board_ioexp_init_ch422(void)
{
    const int attempts = 3;
    esp_err_t probe_err = ESP_FAIL;
    ch422_probe_result_t probe = {0};
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        probe_err = ch422_probe(&probe);
        ch422_log_probe_result(&probe);
        if (probe_err == ESP_OK) {
            break;
        }
        if (attempt < attempts) {
            ESP_LOGW(TAG_CH422, "CH422G probe failed (attempt %d/%d): %s", attempt, attempts, esp_err_to_name(probe_err));
            vTaskDelay(pdMS_TO_TICKS(20 * attempt));
        }
    }

    if (probe_err != ESP_OK) {
        return probe_err;
    }

    esp_err_t add_err = ch422_add_device();
    if (add_err != ESP_OK) {
        return add_err;
    }

    uint8_t sys_param = CH422_CMD_SYS_PARAM | 0x7F;
    esp_err_t sys_err = ch422_send_cmd(&sys_param, 1, "CH422 SYS_PARAM");
    if (sys_err != ESP_OK) {
        ch422_release_handle();
        return sys_err;
    }

    s_io_driver = IO_DRIVER_CH422;
    s_board_has_expander = true;
    s_io_state = 0;
    ESP_LOGI(TAG_CH422, "CH422G init OK, applying defaults");
    return board_io_expander_post_init_defaults();
}

static esp_err_t board_ioexp_init_waveshare(void)
{
    const uint8_t addr = 0x24;
    esp_err_t probe = i2c_master_probe(s_i2c_bus_handle, addr, pdMS_TO_TICKS(120));
    if (probe != ESP_OK) {
        ESP_LOGW(TAG_IO, "Waveshare IO extension did not ACK at 0x%02X: %s", addr, esp_err_to_name(probe));
        return probe;
    }

    io_extension_ws_config_t cfg = {
        .bus = s_i2c_bus_handle,
        .address = addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };

    esp_err_t err = io_extension_ws_init(&cfg, &s_io_ws);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_IO, "Waveshare IO extension init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_io_driver = IO_DRIVER_WAVESHARE;
    s_board_has_expander = true;
    ESP_LOGI(TAG_IO, "Waveshare IO extension ready, applying defaults");
    esp_err_t defaults_err = board_io_expander_post_init_defaults();
    if (defaults_err != ESP_OK) {
        io_extension_ws_deinit(s_io_ws);
        s_io_ws = NULL;
        s_board_has_expander = false;
        s_io_driver = IO_DRIVER_NONE;
        return defaults_err;
    }
    return ESP_OK;
}

static esp_err_t board_io_expander_init(void)
{
    ESP_LOGI(TAG_IO, "Initializing IO expander backend...");
    s_board_has_expander = false;
    s_io_driver = IO_DRIVER_NONE;
    s_io_ws = NULL;

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG_IO, "I2C bus not ready for IO expander");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;

#if CONFIG_BOARD_IOEXP_DRIVER_WAVESHARE
    err = board_ioexp_init_waveshare();
    if (err != ESP_OK) {
#if CONFIG_BOARD_IOEXP_ALLOW_CH422G_FALLBACK
        ESP_LOGW(TAG_IO, "Falling back to CH422G backend after Waveshare init failure (%s)", esp_err_to_name(err));
        err = board_ioexp_init_ch422();
#endif
    }
#elif CONFIG_BOARD_IOEXP_DRIVER_CH422G
    err = board_ioexp_init_ch422();
#endif

    if (err != ESP_OK) {
        s_board_has_expander = false;
        s_io_driver = IO_DRIVER_NONE;
        s_logged_expander_absent = true;
#if CONFIG_BOARD_REQUIRE_IO_EXPANDER
        esp_system_abort("IO expander missing and required");
#endif
        return err;
    }

    ESP_LOGI(TAG_IO, "IO expander backend ready (%s)",
             s_io_driver == IO_DRIVER_WAVESHARE ? "Waveshare CH32V003" : "CH422G");
    vTaskDelay(pdMS_TO_TICKS(50));
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

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &s_lcd_panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_reset(s_lcd_panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel reset failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_init(s_lcd_panel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_board_has_lcd = true;
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

    bool can_reset = s_board_has_expander;
    if (!can_reset) {
        ESP_LOGW(TAG_TOUCH, "IO expander unavailable, probing GT911 without reset");
    }

    uint8_t gt911_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    ESP_RETURN_ON_ERROR(gt911_detect_i2c_addr(&gt911_addr, can_reset), TAG_TOUCH, "GT911 detection failed");

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

    esp_err_t err = esp_lcd_new_panel_io_i2c(s_i2c_bus_handle, &io_conf, &io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_TOUCH, "GT911 panel IO create failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };

    err = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &s_touch_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_TOUCH, "GT911 init OK on I2C addr 0x%02X, INT=%d", gt911_addr, BOARD_TOUCH_INT);
        uint16_t sample_x[1] = {0};
        uint16_t sample_y[1] = {0};
        uint8_t sample_cnt = 0;
        bool pressed = board_touch_fetch(sample_x, sample_y, &sample_cnt);
        ESP_LOGI(TAG_TOUCH, "GT911 sample: pressed=%d count=%u x=%u y=%u", pressed, sample_cnt, sample_x[0], sample_y[0]);
    }
    return err;
}

static esp_err_t gt911_detect_i2c_addr(uint8_t *addr_out, bool can_reset)
{
    ESP_RETURN_ON_FALSE(addr_out, ESP_ERR_INVALID_ARG, TAG_TOUCH, "GT911 addr_out is NULL");

    const uint8_t candidates[] = {0x5D, 0x14};
    bool detected = false;

    int max_attempts = can_reset ? 2 : 1;
    for (int attempt = 0; attempt < max_attempts && !detected; ++attempt) {
        bool int_level = (attempt == 0);
        gpio_config_t int_cfg = {
            .pin_bit_mask = BIT64(BOARD_TOUCH_INT),
            .mode = can_reset ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG_TOUCH, "GT911 INT config failed (attempt %d)", attempt + 1);
        if (can_reset) {
            ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_TOUCH_INT, int_level), TAG_TOUCH, "GT911 INT drive failed");

            ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, false), TAG_TOUCH, "GT911 reset low failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true), TAG_TOUCH, "GT911 reset high failed");
            vTaskDelay(pdMS_TO_TICKS(80));
        } else if (attempt == 0) {
            ESP_LOGW(TAG_TOUCH, "GT911 reset skipped: IO expander unavailable, probing passive state");
        }

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
        ESP_LOGE(TAG_TOUCH, "GT911 not found at 0x5D or 0x14 after probing%s", can_reset ? " with resets" : "");
        return ESP_ERR_NOT_FOUND;
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

    if (!s_board_has_expander) {
        if (!s_logged_expander_absent) {
            ESP_LOGW(TAG_SD, "Skipping SD mount: IO expander unavailable for CS control");
            s_logged_expander_absent = true;
        }
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG_SD, "Initializing SD Card via SPI (CS=EXIO4 via IO extender)...");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_MOSI,
        .miso_io_num = BOARD_SD_MISO,
        .sclk_io_num = BOARD_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = BOARD_SD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    const char mount_point[] = "/sdcard";
    static const int freq_table_khz[] = {400, 1000, 5000, 20000};
    const size_t max_attempts = sizeof(freq_table_khz) / sizeof(freq_table_khz[0]);
    esp_err_t ret = ESP_FAIL;

    // Ensure CS idles high before the first clock train
    io_expander_sd_cs(false);
    board_delay_for_sd();

    for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
        sdspi_ioext_config_t ioext_cfg = {
            .spi_host = BOARD_SD_SPI_HOST,
            .bus_cfg = &bus_cfg,
            .slot_config = slot_config,
            .set_cs_cb = io_expander_sd_cs,
            .cs_user_ctx = NULL,
            .cs_setup_delay_us = 5,
            .cs_hold_delay_us = 5,
            .initial_clocks = 80,
        };

        sdmmc_host_t host = {0};
        host.max_freq_khz = freq_table_khz[attempt];
        ret = sdspi_ioext_host_init(&ioext_cfg, &host, &s_sdspi_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_SD, "SD host init failed @%dkHz: %s", freq_table_khz[attempt], esp_err_to_name(ret));
            continue;
        }

        ESP_LOGI(TAG_SD, "SD attempt %d/%d @ %dkHz (MISO=%d MOSI=%d SCLK=%d CS=EXIO%u)",
                 (int)(attempt + 1), (int)max_attempts, freq_table_khz[attempt],
                 BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CLK, IO_EXP_PIN_SD_CS);

        ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &s_card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_SD, "SD Card mounted at %s", mount_point);
            sdmmc_card_print_info(stdout, s_card);
            return ESP_OK;
        }

        ESP_LOGW(TAG_SD, "SD mount failed (attempt %d): %s", attempt + 1, esp_err_to_name(ret));
        sdspi_ioext_host_deinit(s_sdspi_handle, BOARD_SD_SPI_HOST, false);
        s_sdspi_handle = 0;
        io_expander_sd_cs(false);
        board_delay_for_sd();
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
    esp_err_t status = ESP_OK;

    esp_err_t i2c_err = board_i2c_init();
    if (i2c_err != ESP_OK) {
        ESP_LOGE(TAG_I2C, "I2C init failed: %s (continuing with limited features)", esp_err_to_name(i2c_err));
        status = i2c_err;
    }

    esp_err_t expander_err = board_io_expander_init();
    if (expander_err != ESP_OK) {
        s_board_has_expander = false;
        ESP_LOGE(TAG_IO, "IO expander init failed: %s (continuing without EXIO)", esp_err_to_name(expander_err));
        status = (status == ESP_OK) ? expander_err : status;
    }

    esp_err_t lcd_err = board_lcd_init();
    if (lcd_err != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(lcd_err));
        status = (status == ESP_OK) ? lcd_err : status;
    }

    esp_err_t touch_err = board_touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG_TOUCH, "Touch init failed: %s (touch disabled)", esp_err_to_name(touch_err));
        s_touch_handle = NULL;
        status = (status == ESP_OK) ? touch_err : status;
    }

    esp_err_t sd_err = board_mount_sdcard();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG_SD, "SD card not mounted: %s", esp_err_to_name(sd_err));
        s_card = NULL;
    }

    esp_err_t calib_err = board_load_battery_calibration();
    if (calib_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery calibration defaults in use (%u-%u) (err=%s)",
                 (unsigned)s_batt_raw_empty, (unsigned)s_batt_raw_full, esp_err_to_name(calib_err));
    }

#if CONFIG_BOARD_BATTERY_ENABLE
    uint8_t batt_pct = 0;
    uint8_t batt_raw = 0;
    esp_err_t batt_err = board_get_battery_level(&batt_pct, &batt_raw);
    if (batt_err == ESP_OK) {
        ESP_LOGI(TAG, "Battery raw=%u, empty=%u, full=%u -> %u%%", batt_raw,
                 (unsigned)s_batt_raw_empty,
                 (unsigned)s_batt_raw_full,
                 batt_pct);
    } else if (batt_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Battery read failed: %s", esp_err_to_name(batt_err));
    }
#endif

    return status;
}

esp_err_t board_set_backlight_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (!s_board_has_expander) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_io_driver == IO_DRIVER_WAVESHARE) {
        ESP_LOGI(TAG, "Backlight set to %u%% via Waveshare PWM", percent);
        return io_expander_set_pwm(percent);
    }
    // CH422G lacks PWM, fall back to digital
    ESP_LOGI(TAG, "Backlight set to %u%% (digital) via CH422G", percent);
    esp_err_t err = io_expander_set_output(IO_EXP_PIN_BK, percent > 0);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t board_io_expander_read_inputs(uint8_t *inputs)
{
    if (!inputs) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_board_has_expander) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (s_io_driver) {
    case IO_DRIVER_WAVESHARE:
        return io_extension_ws_read_inputs(s_io_ws, inputs);
    case IO_DRIVER_CH422:
        return ch422_read_inputs(inputs);
    default:
        return ESP_ERR_INVALID_STATE;
    }
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

#if !CONFIG_BOARD_BATTERY_ENABLE
    return ESP_ERR_INVALID_STATE;
#endif

    if (!s_board_has_expander) {
        if (!s_logged_batt_unavailable) {
            ESP_LOGW(TAG_IO, "Battery sense unavailable: IO expander not initialized");
            s_logged_batt_unavailable = true;
        }
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

bool board_has_io_expander(void)
{
    return s_board_has_expander;
}

bool board_touch_is_ready(void)
{
    return s_touch_handle != NULL;
}

bool board_sd_is_mounted(void)
{
    return s_card != NULL;
}

bool board_lcd_is_ready(void)
{
    return s_board_has_lcd;
}

