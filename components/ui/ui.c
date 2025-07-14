#include "ui.h"
#include "lvgl.h"
#include "power.h"
#include "backlight.h"

typedef enum {
    UI_STR_HOME_TITLE,
    UI_STR_SETTINGS_TITLE,
    UI_STR_ENERGY_USAGE,
    UI_STR_LANGUAGE_EN,
    UI_STR_LANGUAGE_FR,
    UI_STR_BRIGHTNESS,
    UI_STR_COUNT
} ui_str_id_t;

static const char *s_lang_table[2][UI_STR_COUNT] = {
    [UI_LANG_EN] = {"Home", "Settings", "Energy", "English", "French", "Brightness"},
    [UI_LANG_FR] = {"Accueil", "Param\xC3\xA8tres", "Energie", "Anglais", "Fran\xC3\xA7ais", "Luminosit\xC3\xA9"}
};

static ui_lang_t s_lang = UI_LANG_EN;

static lv_obj_t *home_screen;
static lv_obj_t *settings_screen;
static lv_obj_t *network_screen;
static lv_obj_t *energy_bar;
static lv_obj_t *home_title;
static lv_obj_t *settings_title;
static lv_obj_t *network_title;
static lv_obj_t *btn_en;
static lv_obj_t *btn_fr;
static lv_obj_t *error_label;
static lv_obj_t *network_label;
static lv_obj_t *brightness_label;
static lv_obj_t *brightness_slider;

static const char *get_str(ui_str_id_t id)
{
    return s_lang_table[s_lang][id];
}

static void lang_event_cb(lv_event_t *e)
{
    ui_lang_t lang = (ui_lang_t)lv_event_get_user_data(e);
    ui_set_language(lang);
}

static void brightness_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t value = lv_slider_get_value(slider);
    backlight_set((uint8_t)value);
}

esp_err_t ui_init(void)
{
    lv_theme_t *th = lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_RED), false,
                                           LV_FONT_DEFAULT);
    lv_disp_set_theme(NULL, th);

    home_screen = lv_obj_create(NULL);
    home_title = lv_label_create(home_screen);
    lv_obj_align(home_title, LV_ALIGN_TOP_MID, 0, 10);

    energy_bar = lv_bar_create(home_screen);
    lv_obj_set_size(energy_bar, 200, 20);
    lv_obj_align(energy_bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(energy_bar, 0, 100);

    error_label = lv_label_create(home_screen);
    lv_obj_align(error_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_color(error_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(error_label, "");

    settings_screen = lv_obj_create(NULL);
    settings_title = lv_label_create(settings_screen);
    lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 10);

    network_screen = lv_obj_create(NULL);
    network_title = lv_label_create(network_screen);
    lv_obj_align(network_title, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(network_title, "Network");
    network_label = lv_label_create(network_screen);
    lv_obj_align(network_label, LV_ALIGN_CENTER, 0, 0);

    btn_en = lv_btn_create(settings_screen);
    lv_obj_align(btn_en, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *lbl_en = lv_label_create(btn_en);
    lv_obj_center(lbl_en);
    lv_obj_add_event_cb(btn_en, lang_event_cb, LV_EVENT_CLICKED, (void *)UI_LANG_EN);

    btn_fr = lv_btn_create(settings_screen);
    lv_obj_align(btn_fr, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_t *lbl_fr = lv_label_create(btn_fr);
    lv_obj_center(lbl_fr);
    lv_obj_add_event_cb(btn_fr, lang_event_cb, LV_EVENT_CLICKED, (void *)UI_LANG_FR);

    brightness_label = lv_label_create(settings_screen);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 40);

    brightness_slider = lv_slider_create(settings_screen);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 60);
    lv_slider_set_range(brightness_slider, 0, 255);
    lv_slider_set_value(brightness_slider, 128, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ui_set_language(s_lang);
    lv_scr_load(home_screen);
    return ESP_OK;
}

void ui_set_language(ui_lang_t lang)
{
    s_lang = lang;
    lv_label_set_text(home_title, get_str(UI_STR_HOME_TITLE));
    lv_label_set_text(settings_title, get_str(UI_STR_SETTINGS_TITLE));
    lv_label_set_text(lv_obj_get_child(btn_en, 0), get_str(UI_STR_LANGUAGE_EN));
    lv_label_set_text(lv_obj_get_child(btn_fr, 0), get_str(UI_STR_LANGUAGE_FR));
    lv_label_set_text(brightness_label, get_str(UI_STR_BRIGHTNESS));
}

void ui_show_home(void)
{
    lv_scr_load(home_screen);
}

void ui_show_settings(void)
{
    lv_scr_load(settings_screen);
}

void ui_show_network(void)
{
    lv_scr_load(network_screen);
}

void ui_update(void)
{
    lv_bar_set_value(energy_bar, power_get_usage_percent(), LV_ANIM_OFF);
}

void ui_update_network(const char *ssid, const char *ip, bool ble_connected)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "SSID: %s\nIP: %s\nBLE: %s",
             ssid,
             ip,
             ble_connected ? "Connected" : "Advertising");
    lv_label_set_text(network_label, buf);
}

void ui_show_error(const char *msg)
{
    lv_label_set_text(error_label, msg);
}

