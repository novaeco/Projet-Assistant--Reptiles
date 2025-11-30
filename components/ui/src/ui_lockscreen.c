#include "ui_lockscreen.h"
#include "ui.h"
#include "reptile_storage.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t * ta_pin;
static char current_pin[8] = {0};

static void ui_show_msgbox(const char *title, const char *text)
{
    lv_obj_t * mbox = lv_msgbox_create(NULL);
    if (title) lv_msgbox_add_title(mbox, title);
    if (text) lv_msgbox_add_text(mbox, text);
    lv_msgbox_add_close_button(mbox);
    lv_msgbox_add_footer_button(mbox, "OK");
    lv_obj_center(mbox);
}

static void kb_event_cb(lv_event_t * e)
{
    lv_obj_t * kb = lv_event_get_target(e);
    const char * txt = lv_buttonmatrix_get_button_text(kb, lv_buttonmatrix_get_selected_button(kb));

    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        const char * entered = lv_textarea_get_text(ta_pin);
        
        // Check PIN
        if (storage_nvs_get_str("sys_pin", current_pin, sizeof(current_pin)) != ESP_OK) {
            // No PIN set, allow anything or should not be here
            ui_create_dashboard();
            return;
        }

        if (strcmp(entered, current_pin) == 0) {
            ui_create_dashboard();
        } else {
            lv_textarea_set_text(ta_pin, "");
            ui_show_msgbox("Erreur", "Code PIN Incorrect");
        }
    }
}

void ui_create_lockscreen(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t * label = lv_label_create(scr);
    lv_label_set_text(label, "Entrez le Code PIN");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);

    ta_pin = lv_textarea_create(scr);
    lv_textarea_set_password_mode(ta_pin, true);
    lv_textarea_set_one_line(ta_pin, true);
    lv_obj_set_width(ta_pin, 200);
    lv_obj_align(ta_pin, LV_ALIGN_TOP_MID, 0, 90);

    static const char * btnm_map[] = {"1", "2", "3", "\n",
                                      "4", "5", "6", "\n",
                                      "7", "8", "9", "\n",
                                      LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};

    lv_obj_t * btnm = lv_buttonmatrix_create(scr);
    lv_buttonmatrix_set_map(btnm, btnm_map);
    lv_obj_set_size(btnm, 200, 200);
    lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btnm, kb_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Link textarea to button matrix logic manually or use keyboard
    // Here we use simple button matrix events to append text
    // Actually, standard keyboard is easier but let's stick to simple numeric pad logic
    // Re-implementing kb_event_cb to handle input properly
    
    // Override event callback for proper input handling
    lv_obj_remove_event_cb(btnm, kb_event_cb);
    lv_obj_add_event_cb(btnm, (lv_event_cb_t)lv_keyboard_def_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_keyboard_set_textarea(btnm, ta_pin); // Treat btnm as keyboard if compatible or just use logic below
    
    // Since lv_buttonmatrix is not lv_keyboard, we need custom logic or use lv_keyboard with NUMERIC mode
    lv_obj_delete(btnm);
    
    lv_obj_t * kb = lv_keyboard_create(scr);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, ta_pin);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL); // OK button

    lv_screen_load(scr);
}