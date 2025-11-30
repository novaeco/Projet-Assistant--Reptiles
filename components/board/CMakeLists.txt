idf_component_register(SRCS "src/board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c driver fatfs vfs)
