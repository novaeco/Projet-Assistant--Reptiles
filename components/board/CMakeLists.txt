idf_component_register(SRCS "src/board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_lcd_touch_gt911 driver fatfs vfs)
