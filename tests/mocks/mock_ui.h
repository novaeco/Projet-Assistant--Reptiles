#pragma once
void ui_show_error(const char *msg);
void ui_update_network(const char *ssid, const char *ip, bool ble_connected);
extern int mock_ui_show_home_count;
extern int mock_ui_show_settings_count;
extern int mock_ui_show_network_count;
extern int mock_ui_show_images_count;
extern int mock_ui_update_network_count;
extern char mock_ui_last_ssid[32];
extern char mock_ui_last_ip[16];
extern bool mock_ui_last_ble;
void ui_show_home(void);
void ui_show_settings(void);
void ui_show_network(void);
void ui_show_images(void);
