#include "unity.h"
#include "display.h"
#include "mock_spi.h"

void test_display_dummy(void)
{
    /* Simply ensure the display header can be included and functions link */
    TEST_ASSERT_NOT_NULL(display_init);
    TEST_ASSERT_NOT_NULL(display_update);
}

void test_display_init_spi_bus_fail(void)
{
    mock_spi_bus_initialize_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, display_init(NULL));
    mock_spi_bus_initialize_ret = ESP_OK;
}

void test_display_init_device_fail(void)
{
    mock_spi_bus_add_device_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, display_init(NULL));
    mock_spi_bus_add_device_ret = ESP_OK;
}
