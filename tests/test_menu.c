#include "unity.h"
#include "menu.h"
#include "mock_ui.h"
#include "mock_keyboard.h"
#include "mock_wifi.h"

void setUp(void) {
    mock_ui_show_home_count = 0;
    mock_ui_show_settings_count = 0;
    mock_ui_show_network_count = 0;
    mock_ui_show_images_count = 0;
    mock_ui_update_network_count = 0;
    mock_ui_last_ssid[0] = '\0';
    mock_ui_last_ip[0] = '\0';
    mock_ui_last_ble = false;
    mock_nvs_get_ssid_ret = ESP_FAIL;
    mock_nvs_get_ssid_value[0] = '\0';
}

void test_menu_home(void)
{
    menu_process_keys(1);
    TEST_ASSERT_EQUAL(1, mock_ui_show_home_count);
}

void test_menu_settings(void)
{
    menu_process_keys(2);
    TEST_ASSERT_EQUAL(1, mock_ui_show_settings_count);
}

void test_menu_network(void)
{
    menu_process_keys(4);
    TEST_ASSERT_EQUAL(1, mock_ui_show_network_count);
}

void test_menu_images(void)
{
    menu_process_keys(8);
    TEST_ASSERT_EQUAL(1, mock_ui_show_images_count);
}
