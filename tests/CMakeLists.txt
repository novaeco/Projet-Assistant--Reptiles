idf_component_register(SRCS "test_main.c" "test_display.c" "test_keyboard.c" "Unity/src/unity.c"
                       INCLUDE_DIRS "." "Unity/src" "../components/display/include" "../components/keyboard/include"
                       PRIV_REQUIRES)
