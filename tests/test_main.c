#include "unity.h"

extern void test_display_dummy(void);
extern void test_display_init_spi_bus_fail(void);
extern void test_display_init_device_fail(void);
extern void test_keyboard_initial_state(void);
extern void test_keyboard_init_gpio_fail(void);
extern void test_keyboard_init_task_fail(void);
extern void test_power_usage_percent(void);
extern void test_power_inactivity_timeout(void);
extern void test_network_init_wifi_init_fail(void);
extern void test_network_init_wifi_start_fail(void);

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
    RUN_TEST(test_power_inactivity_timeout);
    RUN_TEST(test_network_init_wifi_init_fail);
    RUN_TEST(test_network_init_wifi_start_fail);
    UNITY_END();
}

