#include "ui_web.h"
#include "ui.h"
#include "net_manager.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t * ta_url;
static lv_obj_t * ta_result;
static lv_obj_t * kb;

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if(code == LV_EVENT_DEFOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void back_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ui_create_dashboard();
    }
}

static void search_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        const char * query = lv_textarea_get_text(ta_url);
        
        if (!net_is_connected()) {
            lv_textarea_set_text(ta_result, "Erreur: Pas de connexion WiFi.");
            return;
        }

        lv_textarea_set_text(ta_result, "Recherche en cours...");
        
        // Construct URL (Mocking a search API or using a public echo for demo)
        // For demo: http://httpbin.org/get?q=<query>
        char url[256];
        snprintf(url, sizeof(url), "http://httpbin.org/get?q=%s", query);
        
        // Buffer for response
        static char response_buf[2048]; // Static to avoid stack overflow
        esp_err_t err = net_http_get(url, response_buf, sizeof(response_buf));
        
        if (err == ESP_OK) {
            lv_textarea_set_text(ta_result, response_buf);
        } else {
            lv_textarea_set_text(ta_result, "Erreur lors de la requete HTTP.");
        }
    }
}

void ui_create_web_screen(void)
{
    // 1. Screen
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);

    // 2. Header
    lv_obj_t * header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_palette_darken(LV_PALETTE_GREY, 3), 0);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Retour");

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "Requêtes Web (Test)");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 3. Content
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_pct(100) - 60);
    lv_obj_set_y(cont, 60);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 20, 0);
    lv_obj_set_style_pad_gap(cont, 15, 0);

    // Search Field
    lv_obj_t * lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Recherche (ex: Python):");
    
    ta_url = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_url, true);
    lv_obj_set_width(ta_url, LV_PCT(100));
    lv_obj_add_event_cb(ta_url, ta_event_cb, LV_EVENT_ALL, NULL);

    // Search Button
    lv_obj_t * btn_search = lv_button_create(cont);
    lv_obj_set_width(btn_search, LV_PCT(50));
    lv_obj_set_style_bg_color(btn_search, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_search, search_event_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn_search);
    lv_label_set_text(lbl, "Rechercher");
    lv_obj_center(lbl);

    // Result Area
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Résultat:");
    
    ta_result = lv_textarea_create(cont);
    lv_obj_set_size(ta_result, LV_PCT(100), 200);
    lv_obj_set_style_text_font(ta_result, LV_FONT_DEFAULT, 0); // Monospace if available would be better
    
    // 4. Keyboard
    kb = lv_keyboard_create(scr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(scr);
}