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
