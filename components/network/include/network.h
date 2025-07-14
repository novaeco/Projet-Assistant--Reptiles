#pragma once

#include "esp_err.h"

/** Initialize Wi-Fi 6 and BLE subsystems */
esp_err_t network_init(void);

/** Periodically update Wi-Fi and BLE status on LVGL display */
void network_update(void);

/** Save Wi-Fi credentials to NVS */
esp_err_t network_save_credentials(const char *ssid, const char *pass);

