#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board hardware (I2C, LCD, Touch, SD)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t board_init(void);

/**
 * @brief Get the LCD panel handle
 *
 * @return esp_lcd_panel_handle_t Handle to the RGB panel
 */
esp_lcd_panel_handle_t board_get_lcd_handle(void);

/**
 * @brief Get the Touch handle
 *
 * @return esp_lcd_touch_handle_t Handle to the touch device
 */
esp_lcd_touch_handle_t board_get_touch_handle(void);

/**
 * @brief Mount the SD card to /sdcard
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t board_mount_sdcard(void);

/**
 * @brief Set LCD backlight brightness (0-100%).
 */
esp_err_t board_set_backlight_percent(uint8_t percent);

/**
 * @brief Read raw IO expander input register (IO0-IO7). IO7 is wired to the
 *        battery-sense input on the Waveshare 7B.
 */
esp_err_t board_io_expander_read_inputs(uint8_t *inputs);

#ifdef __cplusplus
}
#endif

