idf_component_register(SRCS "src/board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_lcd_touch_gt911 esp_driver_i2c esp_driver_spi esp_driver_sdspi fatfs esp_driver_gpio)