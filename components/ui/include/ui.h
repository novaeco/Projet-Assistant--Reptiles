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

