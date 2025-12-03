#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
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
#include "board_io_map.h"
#include "io_extension_waveshare.h"
#include "sdspi_ioext_host.h"
#include "ch422g.h"

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
static io_extension_ws_handle_t s_io_ws = NULL;
static ch422g_handle_t s_ch422 = NULL;
static bool s_board_has_expander = false;
static bool s_board_has_lcd = false;
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

static void board_log_i2c_levels(const char *stage);
static esp_err_t board_i2c_recover_bus(void);
static esp_err_t board_i2c_selftest(void);
static void *board_get_io_cs_ctx(void);

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
static void board_i2c_scan(i2c_master_bus_handle_t bus);
static esp_err_t gt911_detect_i2c_addr(uint8_t *addr_out, bool can_reset);
static bool board_touch_fetch(uint16_t *x, uint16_t *y, uint8_t *count_out);

static bool board_touch_fetch(uint16_t *x, uint16_t *y, uint8_t *count_out)
{
    if (!s_touch_handle) {
        return false;
    }

    uint8_t count = 0;
    esp_lcd_touch_point_data_t point[1] = {0};
    esp_err_t err = ESP_OK;

    esp_lcd_touch_read_data(s_touch_handle);
    err = esp_lcd_touch_get_data(s_touch_handle, point, &count, 1);

    bool pressed = (err == ESP_OK && count > 0);
    if (pressed) {
        *x = point[0].x;
        *y = point[0].y;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG_TOUCH, "esp_lcd_touch_get_data failed: %s", esp_err_to_name(err));
    }

    if (count_out) {
        *count_out = count;
    }
    return pressed;
}

static void *board_get_io_cs_ctx(void)
{
    switch (s_io_driver) {
    case IO_DRIVER_WAVESHARE:
        return s_io_ws;
    case IO_DRIVER_CH422:
        return s_ch422;
    default:
        return NULL;
    }
}

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
        .flags.enable_internal_pullup = CONFIG_BOARD_I2C_ENABLE_INTERNAL_PULLUP,
        .trans_queue_depth = 0, // Force synchronous transfers (async queue depth must be 0 for probe)
    };

    ESP_LOGI(TAG_I2C, "Initializing I2C bus (SDA=%d SCL=%d @ %d Hz, internal PU=%s, queue=%u sync)...", BOARD_I2C_SDA,
             BOARD_I2C_SCL, BOARD_I2C_FREQ_HZ, CONFIG_BOARD_I2C_ENABLE_INTERNAL_PULLUP ? "on" : "off",
             (unsigned)bus_config.trans_queue_depth);
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus_handle), TAG_I2C, "I2C bus create failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(board_i2c_selftest(), TAG_I2C, "I2C selftest failed");

    board_i2c_scan(s_i2c_bus_handle);
    return ESP_OK;
}

static void board_i2c_scan(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        ESP_LOGW(TAG_I2C, "I2C scan skipped: bus handle not ready");
        return;
    }

    esp_log_level_t prev_level = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_WARN);

    ESP_LOGI(TAG_I2C, "Scanning I2C bus for key devices (IO EXT + GT911) @ %d Hz...", BOARD_I2C_FREQ_HZ);

    struct {
        uint8_t addr;
        const char *label;
    } probes[] = {
        {BOARD_IO_EXP_ADDR, "IO_EXT"},
        {0x5D, "GT911_ALT1"},
        {0x14, "GT911_ALT2"},
    };

    char ack_buf[96] = {0};
    size_t ack_len = 0;
    int found = 0;
    const TickType_t timeout_ticks = pdMS_TO_TICKS(150);

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        if (i2c_master_probe(bus, probes[i].addr, timeout_ticks) == ESP_OK) {
            ++found;
            int written = snprintf(&ack_buf[ack_len], sizeof(ack_buf) - ack_len, "%s0x%02X(%s)",
                                   ack_len == 0 ? "" : ", ", probes[i].addr, probes[i].label);
            if (written > 0 && ack_len + (size_t)written < sizeof(ack_buf)) {
                ack_len += (size_t)written;
            }
        }
    }

    if (found > 0) {
        ESP_LOGI(TAG_I2C, "I2C scan ACK (%d/%d): %s", found, (int)(sizeof(probes) / sizeof(probes[0])), ack_buf);
    } else {
        ESP_LOGW(TAG_I2C, "I2C scan: no expected devices responded");
    }

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
        {BOARD_IO_EXP_ADDR, "IO EXT", false},
        {0x5D, "GT911 (alt1)", false},
        {0x14, "GT911 (alt2)", false},
    };

    int ack = 0;
    esp_log_level_t prev_level = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_WARN);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, expected[i].addr, pdMS_TO_TICKS(150));
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

static uint16_t io_expander_cached_state(void)
{
    switch (s_io_driver) {
    case IO_DRIVER_WAVESHARE:
        return 0; // The Waveshare backend keeps its own state
    case IO_DRIVER_CH422:
        return ch422g_get_cached_outputs(s_ch422);
    default:
        return 0;
    }
}

static esp_err_t io_expander_set_output(uint8_t pin, bool level)
{
    if (!s_board_has_expander) {
        return ESP_ERR_INVALID_STATE;
    }
    switch (s_io_driver) {
    case IO_DRIVER_WAVESHARE:
        return io_extension_ws_set_output(s_io_ws, pin, level);
    case IO_DRIVER_CH422:
        return ch422g_set_output_level(s_ch422, pin, level);
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
    if (!ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    bool level = !assert; // Active-low CS
    ESP_LOGD(TAG_SD, "IOEXT SD CS %s (level=%d)", assert ? "ASSERT" : "DEASSERT", (int)level);
    if (ctx == s_io_ws && s_io_driver == IO_DRIVER_WAVESHARE) {
        return io_extension_ws_set_output(s_io_ws, IO_EXP_PIN_SD_CS, level);
    }
    if (ctx == s_ch422 && s_io_driver == IO_DRIVER_CH422) {
        return ch422g_set_output_level(s_ch422, IO_EXP_PIN_SD_CS, level);
    }

    return io_expander_set_output(IO_EXP_PIN_SD_CS, level);
}

static esp_err_t board_io_expander_post_init_defaults(void)
{
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_VDD, true), TAG_IO, "LCD_VDD on failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_BK, true), TAG_IO, "BK default on failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true), TAG_IO, "TP_RST default high failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_SD_CS, true), TAG_IO, "SD CS idle high failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_CAN_USB, false), TAG_IO, "CAN/USB default failed");
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, false), TAG_IO, "LCD_RST low failed");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_LCD_RST, true), TAG_IO, "LCD_RST high failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(io_expander_set_pwm(100), TAG_IO, "PWM init failed");

    uint16_t cached = io_expander_cached_state();
    ESP_LOGI(TAG_IO, "IO expander defaults applied (state=0x%04X)", (unsigned)cached);
    return ESP_OK;
}

static esp_err_t board_ioexp_init_ch422(void)
{
    const int attempts = 3;
    esp_err_t err = ESP_FAIL;
    ch422g_config_t cfg = {
        .bus = s_i2c_bus_handle,
        .address = BOARD_IO_EXP_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        err = ch422g_init(&cfg, &s_ch422);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG_CH422, "CH422G init attempt %d/%d failed: %s", attempt, attempts, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(25 * attempt));
    }

    if (err != ESP_OK) {
        return err;
    }

    uint8_t sys_param = 0;
    err = ch422g_read_reg(s_ch422, 0x00, &sys_param);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_CH422, "CH422G SYS_PARAM=0x%02X", sys_param);
    }

    s_io_driver = IO_DRIVER_CH422;
    s_board_has_expander = true;
    ESP_LOGI(TAG_CH422, "CH422G init OK at 0x%02X, applying defaults", cfg.address);
    err = board_io_expander_post_init_defaults();
    if (err != ESP_OK) {
        ch422g_deinit(s_ch422);
        s_ch422 = NULL;
        s_board_has_expander = false;
        s_io_driver = IO_DRIVER_NONE;
    }
    return err;
}

static esp_err_t board_ioexp_init_waveshare(void)
{
    const uint8_t addr = 0x24;
    esp_err_t probe = i2c_master_probe(s_i2c_bus_handle, addr, pdMS_TO_TICKS(150));
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
    s_ch422 = NULL;

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
        bool int_level = (attempt == 0) ? false : true;
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

            ESP_LOGI(TAG_TOUCH, "GT911 reset attempt %d: drive INT %s, toggling TP_RST via IO expander", attempt + 1,
                     int_level ? "HIGH" : "LOW");
            ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, false), TAG_TOUCH, "GT911 reset low failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            ESP_RETURN_ON_ERROR(io_expander_set_output(IO_EXP_PIN_TOUCH_RST, true), TAG_TOUCH, "GT911 reset high failed");
            vTaskDelay(pdMS_TO_TICKS(80));
        } else if (attempt == 0) {
            ESP_LOGW(TAG_TOUCH, "GT911 reset skipped: IO expander unavailable, probing passive state");
        }

        // Release INT so GT911 can drive it after address selection phase
        gpio_config_t int_input_cfg = {
            .pin_bit_mask = BIT64(BOARD_TOUCH_INT),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_input_cfg), TAG_TOUCH, "GT911 INT restore to input failed (attempt %d)", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(5));

        ESP_LOGI(TAG_TOUCH, "GT911 detect attempt %d: INT %s during reset, probing 0x5D/0x14", attempt + 1,
                 int_level ? "HIGH" : "LOW");
        for (size_t i = 0; i < sizeof(candidates); ++i) {
            uint8_t addr = candidates[i];
            esp_err_t probe = i2c_master_probe(s_i2c_bus_handle, addr, pdMS_TO_TICKS(150));
            if (probe == ESP_OK) {
                *addr_out = addr;
                detected = true;
                ESP_LOGI(TAG_TOUCH, "GT911 acknowledged at 0x%02X after INT %s reset", addr, int_level ? "HIGH" : "LOW");
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
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {0};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    if (!s_board_has_expander) {
        if (!s_logged_expander_absent) {
            ESP_LOGW(TAG_SD, "Skipping SD mount: IO expander unavailable for CS control");
            s_logged_expander_absent = true;
        }
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG_SD, "Initializing SD Card via SPI (CS=EXIO4 via IO extender)...");

    spi_bus_config_t bus_cfg = {0};
    bus_cfg.mosi_io_num = BOARD_SD_MOSI;
    bus_cfg.miso_io_num = BOARD_SD_MISO;
    bus_cfg.sclk_io_num = BOARD_SD_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = BOARD_SD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    const char mount_point[] = "/sdcard";
    static const int freq_table_khz[] = {400, 1000, 5000, 20000};
    const size_t max_attempts = sizeof(freq_table_khz) / sizeof(freq_table_khz[0]);
    esp_err_t ret = ESP_FAIL;

    void *cs_ctx = board_get_io_cs_ctx();

    // Ensure CS idles high before the first clock train
    io_expander_sd_cs(false, cs_ctx);
    board_delay_for_sd();

    bool spi_bus_initialized = false;
    bool spi_bus_owned = false;

    s_card = NULL;

    esp_err_t bus_err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (bus_err == ESP_OK) {
        spi_bus_initialized = true;
        spi_bus_owned = true;
        ESP_LOGI(TAG_SD, "SPI bus initialized for SDSPI host %d", BOARD_SD_SPI_HOST);
    } else if (bus_err == ESP_ERR_INVALID_STATE) {
        spi_bus_initialized = true;
        spi_bus_owned = false;
        ESP_LOGW(TAG_SD, "SPI bus for host %d already initialized, reusing", BOARD_SD_SPI_HOST);
    } else {
        ESP_LOGE(TAG_SD, "SPI bus init failed: %s", esp_err_to_name(bus_err));
        return bus_err;
    }

    for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
        sdspi_ioext_config_t ioext_cfg = {
            .spi_host = BOARD_SD_SPI_HOST,
            .bus_cfg = NULL, // bus already initialized once above
            .slot_config = slot_config,
            .max_freq_khz = freq_table_khz[attempt],
            .set_cs_cb = io_expander_sd_cs,
            .cs_user_ctx = cs_ctx,
            .cs_setup_delay_us = 5,
            .cs_hold_delay_us = 5,
            .initial_clocks = 80,
        };

        sdmmc_host_t host = {0};
        ret = sdspi_ioext_host_init(&ioext_cfg, &host, &s_sdspi_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_SD, "SD host init failed @%dkHz: %s", freq_table_khz[attempt], esp_err_to_name(ret));
            continue;
        }

        if (host.slot != (int)(intptr_t)s_sdspi_handle) {
            ESP_LOGE(TAG_SD, "Host slot/device mismatch (host.slot=%p device=%p)", (void *)(intptr_t)host.slot,
                     (void *)s_sdspi_handle);
            ret = ESP_ERR_INVALID_STATE;
            sdspi_ioext_host_deinit(s_sdspi_handle, BOARD_SD_SPI_HOST, false);
            s_sdspi_handle = 0;
            continue;
        }

        ESP_LOGI(TAG_SD, "SDSPI host prepared (spi_host=%d, device=%p, host.slot=%p, target_freq=%dkHz)",
                 BOARD_SD_SPI_HOST, (void *)s_sdspi_handle, (void *)host.slot, host.max_freq_khz);

        ESP_LOGI(TAG_SD, "SD attempt %d/%d @ %dkHz (MISO=%d MOSI=%d SCLK=%d CS=EXIO%u)",
                 (int)(attempt + 1), (int)max_attempts, freq_table_khz[attempt],
                 BOARD_SD_MISO, BOARD_SD_MOSI, BOARD_SD_CLK, IO_EXP_PIN_SD_CS);

        ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &s_card);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_SD, "SD Card mounted at %s", mount_point);
            sdmmc_card_print_info(stdout, s_card);

            struct stat st = {0};
            if (stat(mount_point, &st) == 0) {
                ESP_LOGI(TAG_SD, "Mount point exists (mode=0x%lx)", (unsigned long)st.st_mode);
            } else {
                ESP_LOGW(TAG_SD, "stat(%s) failed: %s", mount_point, strerror(errno));
            }

            DIR *dir = opendir(mount_point);
            if (dir) {
                ESP_LOGI(TAG_SD, "Listing %s (up to 5 entries):", mount_point);
                struct dirent *entry = NULL;
                int count = 0;
                while (count < 5 && (entry = readdir(dir)) != NULL) {
                    ESP_LOGI(TAG_SD, "  %s", entry->d_name);
                    ++count;
                }
                closedir(dir);
            } else {
                ESP_LOGW(TAG_SD, "opendir(%s) failed: %s", mount_point, strerror(errno));
            }

            return ESP_OK;
        }

        ESP_LOGW(TAG_SD, "SD mount failed (attempt %d): %s", attempt + 1, esp_err_to_name(ret));
        sdspi_ioext_host_deinit(s_sdspi_handle, BOARD_SD_SPI_HOST, false);
        s_sdspi_handle = 0;
        io_expander_sd_cs(false, cs_ctx);
        board_delay_for_sd();
    }

    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG_SD, "Failed to mount filesystem.");
    } else {
        ESP_LOGE(TAG_SD, "Failed to initialize the card (%s).", esp_err_to_name(ret));
    }
    if (spi_bus_initialized) {
        sdspi_ioext_host_deinit(s_sdspi_handle, BOARD_SD_SPI_HOST, spi_bus_owned);
        s_sdspi_handle = 0;
        io_expander_sd_cs(false, cs_ctx);
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

    ESP_LOGI(TAG, "Board features enabled: expander=%d sd=%d touch=%d lcd=%d",
             s_board_has_expander ? 1 : 0,
             board_sd_is_mounted() ? 1 : 0,
             board_touch_is_ready() ? 1 : 0,
             board_lcd_is_ready() ? 1 : 0);

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
        return ch422g_read_inputs(s_ch422, inputs);
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

