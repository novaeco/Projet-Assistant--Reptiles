#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the UI system (LVGL, Display, Input).
 *        This function creates the LVGL task and starts the tick timer.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_init(void);

/**
 * @brief Create the main dashboard screen.
 */
void ui_create_dashboard(void);

#ifdef __cplusplus
}
#endif