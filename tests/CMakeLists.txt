idf_component_register(
    SRCS "test_main.c" "test_display.c" "test_keyboard.c" "test_power.c" "Unity/src/unity.c"
         "mocks/mock_spi.c" "mocks/mock_i2c.c" "mocks/mock_gpio.c" "mocks/mock_freertos.c" "mocks/mock_adc.c"
    INCLUDE_DIRS "." "Unity/src" "mocks"
                 "../components/display/include" "../components/keyboard/include" "../components/touch/include" "../components/power/include"
    PRIV_REQUIRES)
