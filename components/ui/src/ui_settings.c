#include "ui_settings.h"
#include "ui.h"
#include "reptile_storage.h"
#include "net_manager.h"
#include "iot_manager.h"
#include "core_export.h" // Include export
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t * ta_ssid;
static lv_obj_t * ta_pwd;
static lv_obj_t * ta_ota_url;
static lv_obj_t * ta_pin;
static lv_obj_t * kb;

static void back_event_cb(lv_event_t * e)
{
    ui_create_dashboard();
}

static void save_wifi_cb(lv_event_t * e)
{
    const char * ssid = lv_textarea_get_text(ta_ssid);
    const char * pwd = lv_textarea_get_text(ta_pwd);
    
    if (strlen(ssid) > 0) {
        storage_nvs_set_str("wifi_ssid", ssid);
        storage_nvs_set_str("wifi_pwd", pwd);
        net_connect(ssid, pwd);
        lv_obj_t * mbox = lv_msgbox_create(NULL, "Info", "WiFi credentials saved.", NULL, true);
        lv_obj_center(mbox);
    }
}

static void save_pin_cb(lv_event_t * e)
{
    const char * pin = lv_textarea_get_text(ta_pin);
    storage_nvs_set_str("sys_pin", pin);
    lv_obj_t * mbox = lv_msgbox_create(NULL, "Info", "Code PIN enregistre.", NULL, true);
    lv_obj_center(mbox);
}

static void export_cb(lv_event_t * e)
{
    if (core_export_csv("/sdcard/export.csv") == ESP_OK) {
        lv_obj_t * mbox = lv_msgbox_create(NULL, "Succes", "Export CSV termine:\n/sdcard/export.csv", NULL, true);
        lv_obj_center(mbox);
    } else {
        lv_obj_t * mbox = lv_msgbox_create(NULL, "Erreur", "Echec de l'export.", NULL, true);
        lv_obj_center(mbox);
    }
}

static void ota_btn_cb(lv_event_t * e)
{
    const char * url = lv_textarea_get_text(ta_ota_url);
    if (strlen(url) > 0) {
        iot_ota_start(url);
    }
}

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(kb != NULL) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if(code == LV_EVENT_READY) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_create_settings_screen(void)
{
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);

    // Header
    lv_obj_t * header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_palette_darken(LV_PALETTE_GREY, 3), 0);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT " Retour");

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "Parametres");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Content Container
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), lv_pct(100) - 60);
    lv_obj_set_y(cont, 60);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // WiFi
    lv_label_set_text(lv_label_create(cont), "WiFi");
    ta_ssid = lv_textarea_create(cont);
    lv_textarea_set_placeholder_text(ta_ssid, "SSID");
    lv_textarea_set_one_line(ta_ssid, true);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

    ta_pwd = lv_textarea_create(cont);
    lv_textarea_set_placeholder_text(ta_pwd, "Password");
    lv_textarea_set_password_mode(ta_pwd, true);
    lv_textarea_set_one_line(ta_pwd, true);
    lv_obj_add_event_cb(ta_pwd, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * btn_save = lv_button_create(cont);
    lv_obj_add_event_cb(btn_save, save_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_save), "Connecter");

    // Security
    lv_obj_t * l_sec = lv_label_create(cont);
    lv_label_set_text(l_sec, "Securite (Code PIN)");
    lv_obj_set_style_margin_top(l_sec, 20, 0);

    ta_pin = lv_textarea_create(cont);
    lv_textarea_set_placeholder_text(ta_pin, "Nouveau PIN (vide = aucun)");
    lv_textarea_set_password_mode(ta_pin, true);
    lv_textarea_set_one_line(ta_pin, true);
    lv_obj_add_event_cb(ta_pin, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * btn_pin = lv_button_create(cont);
    lv_obj_add_event_cb(btn_pin, save_pin_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_pin), "Sauvegarder PIN");

    // Export
    lv_obj_t * l_exp = lv_label_create(cont);
    lv_label_set_text(l_exp, "Donnees");
    lv_obj_set_style_margin_top(l_exp, 20, 0);

    lv_obj_t * btn_export = lv_button_create(cont);
    lv_obj_add_event_cb(btn_export, export_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_export), "Exporter CSV (SD)");

    // OTA
    lv_obj_t * l_ota = lv_label_create(cont);
    lv_label_set_text(l_ota, "Mise a jour");
    lv_obj_set_style_margin_top(l_ota, 20, 0);

    ta_ota_url = lv_textarea_create(cont);
    lv_textarea_set_placeholder_text(ta_ota_url, "URL");
    lv_textarea_set_one_line(ta_ota_url, true);
    lv_obj_add_event_cb(ta_ota_url, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * btn_ota = lv_button_create(cont);
    lv_obj_add_event_cb(btn_ota, ota_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_ota), "Mettre a jour");

    kb = lv_keyboard_create(scr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(scr);
}