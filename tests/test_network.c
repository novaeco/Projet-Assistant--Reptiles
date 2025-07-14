#include "unity.h"
#include "network.h"
#include "mock_wifi.h"
#include "mock_ui.h"

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

void test_network_update_uses_stored_ssid(void)
{
    strcpy(mock_nvs_get_ssid_value, "TestSSID");
    mock_nvs_get_ssid_ret = ESP_OK;

    TEST_ASSERT_EQUAL(ESP_OK, network_init());
    network_update();

    TEST_ASSERT_EQUAL(1, mock_ui_update_network_count);
    TEST_ASSERT_EQUAL_STRING("TestSSID", mock_ui_last_ssid);

    mock_nvs_get_ssid_ret = ESP_FAIL;
}

