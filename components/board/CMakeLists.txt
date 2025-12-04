set(board_requires driver esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c esp_timer nvs_flash reptile_storage io_extension_waveshare ch422g)

idf_component_register(SRCS "src/board.c" "src/board_backlight.c"
                      INCLUDE_DIRS "include"
                      REQUIRES ${board_requires}
                      PRIV_REQUIRES esp_driver_ledc)
