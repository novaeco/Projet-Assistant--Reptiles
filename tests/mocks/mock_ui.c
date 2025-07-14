#include "mock_ui.h"
int mock_ui_show_home_count = 0;
int mock_ui_show_settings_count = 0;
int mock_ui_show_network_count = 0;
int mock_ui_show_images_count = 0;

void ui_show_error(const char *msg) { (void)msg; }
void ui_update_network(const char *ssid, const char *ip, bool ble_connected) {
    (void)ssid; (void)ip; (void)ble_connected;
}
void ui_show_home(void) { mock_ui_show_home_count++; }
void ui_show_settings(void) { mock_ui_show_settings_count++; }
void ui_show_network(void) { mock_ui_show_network_count++; }
void ui_show_images(void) { mock_ui_show_images_count++; }
