#include "unity.h"

extern void test_display_dummy(void);
extern void test_display_init_spi_bus_fail(void);
extern void test_display_init_device_fail(void);
extern void test_keyboard_initial_state(void);
extern void test_keyboard_init_gpio_fail(void);
extern void test_keyboard_init_task_fail(void);
extern void test_power_usage_percent(void);

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_display_dummy);
    RUN_TEST(test_display_init_spi_bus_fail);
    RUN_TEST(test_display_init_device_fail);
    RUN_TEST(test_keyboard_initial_state);
    RUN_TEST(test_keyboard_init_gpio_fail);
    RUN_TEST(test_keyboard_init_task_fail);
    RUN_TEST(test_power_usage_percent);
    UNITY_END();
}
