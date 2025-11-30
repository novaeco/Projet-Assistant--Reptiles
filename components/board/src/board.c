#include <stdio.h>
#include "board.h"
#include "board_pins.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "BOARD";

static esp_lcd_panel_handle_t s_lcd_panel_handle = NULL;
static void* s_touch_handle = NULL; // Placeholder for touch handle
static sdmmc_card_t *s_card = NULL;

// =============================================================================
// I2C & IO Expander
// =============================================================================

static esp_err_t board_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    ESP_LOGI(TAG, "Initializing I2C...");
    ESP_ERROR_CHECK(i2c_param_config(BOARD_I2C_PORT, &conf));
    return i2c_driver_install(BOARD_I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t board_io_expander_init(void)
{
    // Placeholder for CH422G initialization
    // Typically involves writing to the I2C address to configure pins for Backlight/Reset
    ESP_LOGI(TAG, "Initializing IO Expander (CH422G) stub...");
    
    // Example: Write to CH422G to enable backlight (assuming generic logic)
    // uint8_t data[] = { 0x01, 0xFF }; // Dummy command
    // i2c_master_write_to_device(BOARD_I2C_PORT, BOARD_IO_EXP_ADDR, data, sizeof(data), 1000 / portTICK_PERIOD_MS);
    
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
        .psram_trans_align = 64,
        .num_fbufs = 2,   // Double buffer in PSRAM
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
            // The following timings are critical and depend on the specific panel datasheet.
            // Using generic safe values for 1024x600.
            .hsync_back_porch = 40,
            .hsync_front_porch = 20,
            .hsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 4,
            .vsync_pulse_width = 4,
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
    ESP_LOGI(TAG, "Initializing Touch (GT911) stub...");
    // Real implementation would use esp_lcd_touch_new_i2c_gt911
    // Here we just ensure I2C is up.
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
    host.slot = BOARD_SD_SPI_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_MOSI,
        .miso_io_num = BOARD_SD_MISO,
        .sclk_io_num = BOARD_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Note: If SPI bus is shared, check if it's already initialized.
    // Here we assume we initialize it.
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BOARD_SD_CS;
    slot_config.host_id = host.slot;

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
    // SD Card mounting is usually called separately or here depending on needs.
    // We'll leave it to be called explicitly or add it here if critical.
    // Let's call it here but not crash if fails (no card inserted).
    board_mount_sdcard(); 
    
    return ESP_OK;
}

esp_lcd_panel_handle_t board_get_lcd_handle(void)
{
    return s_lcd_panel_handle;
}

void* board_get_touch_handle(void)
{
    return s_touch_handle;
}