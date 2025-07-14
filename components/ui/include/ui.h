#pragma once

#include "esp_err.h"

typedef enum {
    UI_LANG_EN,
    UI_LANG_FR,
} ui_lang_t;

/** Initialize screens and theme */
esp_err_t ui_init(void);

/** Set current language */
void ui_set_language(ui_lang_t lang);

/** Show home screen */
void ui_show_home(void);

/** Show settings screen */
void ui_show_settings(void);

/** Update widgets like energy usage */
void ui_update(void);

/** Display an error message */
void ui_show_error(const char *msg);

