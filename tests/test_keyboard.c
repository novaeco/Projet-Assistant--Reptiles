#include "unity.h"
#include "keyboard.h"
#include "mock_gpio.h"
#include "mock_freertos.h"

void test_keyboard_initial_state(void)
{
    /* Without initialization keyboard state should be zero */
    TEST_ASSERT_EQUAL_UINT16(0, keyboard_get_state());
}

void test_keyboard_init_gpio_fail(void)
{
    mock_gpio_config_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, keyboard_init());
    mock_gpio_config_ret = ESP_OK;
}

void test_keyboard_init_task_fail(void)
{
    mock_xTaskCreate_ret = pdFAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, keyboard_init());
    mock_xTaskCreate_ret = pdPASS;
}
