idf_component_register(
    SRCS "test_main.c" "test_display.c" "test_keyboard.c" "Unity/src/unity.c"
         "mocks/mock_spi.c" "mocks/mock_i2c.c" "mocks/mock_gpio.c" "mocks/mock_freertos.c"
    INCLUDE_DIRS "." "Unity/src" "mocks"
                 "../components/display/include" "../components/keyboard/include" "../components/touch/include"
    PRIV_REQUIRES)
