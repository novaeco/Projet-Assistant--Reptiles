idf_component_register(SRCS "src/board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c fatfs vfs sdmmc esp_timer nvs_flash reptile_storage)
