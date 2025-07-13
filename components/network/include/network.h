#pragma once

/** Initialize Wi-Fi 6 and BLE subsystems */
void network_init(void);

/** Periodically update network status on LVGL display */
void network_update(void);

