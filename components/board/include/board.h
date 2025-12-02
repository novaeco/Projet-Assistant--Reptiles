#pragma once

#include <stdbool.h>
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
 * @brief Check if the IO expander (CH422G) was successfully initialized.
 */
bool board_has_io_expander(void);

/**
 * @brief Check if the GT911 touch driver is ready.
 */
bool board_touch_is_ready(void);

/**
 * @brief Check if the SD card is mounted.
 */
bool board_sd_is_mounted(void);

/**
 * @brief Check if the RGB LCD panel is initialized.
 */
bool board_lcd_is_ready(void);

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

/**
 * @brief Read the battery level from the IO expander and convert it to
 *        a 0-100% percentage using the calibrated raw full/empty values from
 *        menuconfig (CONFIG_BOARD_BATTERY_RAW_FULL/EMPTY) or values stored in NVS.
 *
 * @param[out] percent Battery level (0-100%).
 * @param[out] raw Optional raw 8-bit value returned by the expander (0-255).
 *
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t board_get_battery_level(uint8_t *percent, uint8_t *raw);

/**
 * @brief Persist battery calibration raw values to NVS and update runtime copy.
 */
esp_err_t board_battery_set_calibration(uint8_t raw_empty, uint8_t raw_full);

/**
 * @brief Read the current battery calibration (defaults or NVS-backed).
 */
esp_err_t board_battery_get_calibration(uint8_t *raw_empty, uint8_t *raw_full);

/**
 * @brief Route the shared connector to the CAN transceiver or USB bridge.
 *
 * @param enable_can True to enable CAN transceiver mode (IO expander pin
 *                   IO_EXP_PIN_CAN_USB high), false to keep USB bridge active.
 */
esp_err_t board_set_can_mode(bool enable_can);

#ifdef __cplusplus
}
#endif

