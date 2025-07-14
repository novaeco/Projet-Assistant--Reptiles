#include "mock_ui.h"
void ui_show_error(const char *msg) { (void)msg; }
void ui_update_network(const char *ssid, const char *ip, bool ble_connected) {
    (void)ssid; (void)ip; (void)ble_connected;
}
