#include "unity.h"
#include "power.h"
#include "mock_adc.h"

void test_power_usage_percent(void)
{
    mock_adc1_raw_value = 0;
    TEST_ASSERT_EQUAL_UINT8(0, power_get_usage_percent());

    mock_adc1_raw_value = 2047; // approx half of 4095
    TEST_ASSERT_UINT8_WITHIN(1, 50, power_get_usage_percent());

    mock_adc1_raw_value = 4095;
    TEST_ASSERT_EQUAL_UINT8(100, power_get_usage_percent());
}
#include "mock_freertos.h"
#include "mock_esp.h"

void test_power_inactivity_timeout(void)
{
    mock_esp_timer_us = 0;
    mock_light_sleep_count = 0;
    mock_pm_acquire_count = 0;
    mock_pm_release_count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, power_init());
    TEST_ASSERT_NOT_NULL(mock_last_task_func);

    mock_esp_timer_us = 31LL * 1000000LL;
    mock_last_task_func(NULL);

    TEST_ASSERT_EQUAL(1, mock_light_sleep_count);
    TEST_ASSERT_EQUAL(1, mock_pm_release_count);
    TEST_ASSERT_EQUAL(1, mock_pm_acquire_count);
}

