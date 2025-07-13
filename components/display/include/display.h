#pragma once

/** Initialize ST7789 display and LVGL */
void display_init(void);

/** Periodically call from main loop to update LVGL */
void display_update(void);

