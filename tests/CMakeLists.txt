idf_component_register(
    SRCS "test_main.c" "test_display.c" "test_keyboard.c" "test_power.c" "test_network.c" "test_menu.c" "Unity/src/unity.c"
         "mocks/mock_spi.c" "mocks/mock_i2c.c" "mocks/mock_gpio.c" "mocks/mock_freertos.c" "mocks/mock_adc.c" "mocks/mock_esp.c" "mocks/mock_wifi.c" "mocks/mock_ui.c" "mocks/mock_keyboard.c"
    INCLUDE_DIRS "." "Unity/src" "mocks"
                 "../components/display/include" "../components/keyboard/include" "../components/touch/include" "../components/power/include" "../components/network/include" "../components/ui/include" "../components/menu/include"
    PRIV_REQUIRES)

