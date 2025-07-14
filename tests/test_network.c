#include "unity.h"
#include "network.h"
#include "mock_wifi.h"

void test_network_init_wifi_init_fail(void)
{
    mock_esp_wifi_init_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, network_init());
    mock_esp_wifi_init_ret = ESP_OK;
}

void test_network_init_wifi_start_fail(void)
{
    mock_esp_wifi_start_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, network_init());
    mock_esp_wifi_start_ret = ESP_OK;
}

