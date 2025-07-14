#pragma once

#include "esp_err.h"

typedef enum {
    UI_LANG_EN,
    UI_LANG_FR,
    UI_LANG_COUNT,
} ui_lang_t;

typedef enum {
    UI_PAGE_HOME,
    UI_PAGE_SETTINGS,
    UI_PAGE_NETWORK,
    UI_PAGE_IMAGES,
    UI_PAGE_COUNT
} ui_page_t;

typedef enum {
    UI_STR_HOME_TITLE,
    UI_STR_SETTINGS_TITLE,
    UI_STR_NETWORK_TITLE,
    UI_STR_IMAGES_TITLE,
    UI_STR_ENERGY_USAGE,
    UI_STR_LANGUAGE_EN,
    UI_STR_LANGUAGE_FR,
    UI_STR_BRIGHTNESS,
    UI_STR_NETWORK_FORMAT,
    UI_STR_BLE_CONNECTED,
    UI_STR_BLE_ADVERTISING,
    UI_STR_WIFI_TITLE,
    UI_STR_WIFI_SSID,
    UI_STR_WIFI_PASS,
    UI_STR_WIFI_SAVE,
    UI_STR_CALIBRATE,
    UI_STR_WIFI_DISCONNECTED,
    UI_STR_BLE_DISCONNECTED,
    UI_STR_SD_REMOVED,
    UI_STR_NET_INIT_FAILED,
    UI_STR_COUNT
} ui_str_id_t;

const char *ui_get_str(ui_str_id_t id);

/** Load or replace a language table */
void ui_load_language(ui_lang_t lang, const char *const table[]);

/** Initialize screens and theme */
esp_err_t ui_init(void);

/** Set current language */
void ui_set_language(ui_lang_t lang);

/** Show home screen */
void ui_show_home(void);

/** Show settings screen */
void ui_show_settings(void);

/** Show network status screen */
void ui_show_network(void);

/** Show Wi-Fi setup screen */
void ui_show_wifi_setup(void);

/** Update the network status information */
void ui_update_network(const char *ssid, const char *ip, bool ble_connected);

/** Update widgets like energy usage */
void ui_update(void);

/** Display an error message */
void ui_show_error(const char *msg);

/** Show SD card image browser */
void ui_show_images(void);

/** Highlight the active navigation item */
void ui_set_active_page(ui_page_t page);

