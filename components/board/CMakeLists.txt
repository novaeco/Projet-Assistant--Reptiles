set(board_requires driver esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c esp_timer nvs_flash reptile_storage io_extension_waveshare ch422g)
if(CONFIG_BOARD_ENABLE_SD)
    list(APPEND board_requires fatfs vfs sdmmc sdspi_ioext_host)
endif()

idf_component_register(SRCS "src/board.c"
                      INCLUDE_DIRS "include"
                      REQUIRES ${board_requires})
