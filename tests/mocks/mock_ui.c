#include "mock_ui.h"
int mock_ui_show_home_count = 0;
int mock_ui_show_settings_count = 0;
int mock_ui_show_network_count = 0;
int mock_ui_show_images_count = 0;
int mock_ui_update_network_count = 0;
char mock_ui_last_ssid[32];
char mock_ui_last_ip[16];
bool mock_ui_last_ble;

void ui_show_error(const char *msg) { (void)msg; }
void ui_update_network(const char *ssid, const char *ip, bool ble_connected) {
    strncpy(mock_ui_last_ssid, ssid, sizeof(mock_ui_last_ssid));
    mock_ui_last_ssid[sizeof(mock_ui_last_ssid) - 1] = '\0';
    strncpy(mock_ui_last_ip, ip, sizeof(mock_ui_last_ip));
    mock_ui_last_ip[sizeof(mock_ui_last_ip) - 1] = '\0';
    mock_ui_last_ble = ble_connected;
    mock_ui_update_network_count++;
}
void ui_show_home(void) { mock_ui_show_home_count++; }
void ui_show_settings(void) { mock_ui_show_settings_count++; }
void ui_show_network(void) { mock_ui_show_network_count++; }
void ui_show_images(void) { mock_ui_show_images_count++; }
