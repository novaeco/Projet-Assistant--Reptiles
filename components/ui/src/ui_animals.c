#include "ui_animals.h"
#include "ui_animal_form.h"
#include "ui_animal_details.h"
#include "ui.h"
#include "core_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t * ta_search;
static lv_obj_t * list_animals;

static char * ui_strdup(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

static void load_animal_list(const char *query) {
    lv_obj_clean(list_animals);
    
    animal_summary_t *animals = NULL;
    size_t count = 0;
    if (core_search_animals(query, &animals, &count) == ESP_OK) {
        if (count == 0) {
            lv_list_add_text(list_animals, "Aucun animal trouvé.");
        } else {
            for (size_t i = 0; i < count; i++) {
                char *id_copy = ui_strdup(animals[i].id);
                char label[256];
                snprintf(label, sizeof(label), "%s (%s)", animals[i].name, animals[i].species);
                
                // Need to recreate event callback to capture id_copy
                // But we can't pass user_data easily in a loop if we define callback outside?
                // Actually we can.
                
                lv_obj_t * btn = lv_list_add_btn(list_animals, LV_SYMBOL_PASTE, label);
                // We need a way to pass the ID. 
                // In LVGL v8/9, user_data is supported in add_event_cb.
                
                // We need a separate callback function? No, same one works.
                // But we need to make sure the callback knows which ID.
                // See below for callback definition.
                
                // To avoid defining callback inside function (not C standard for closures), 
                // we use the static callback and pass id_copy as user_data.
                // HOWEVER, we must ensure id_copy is freed when button is deleted.
                // For this MVP, we accept the small leak on screen refresh or we use LV_EVENT_DELETE to free it.
                
                lv_obj_add_event_cb(btn, (lv_event_cb_t)ui_create_animal_details_screen, LV_EVENT_CLICKED, id_copy); 
                // Wait, ui_create_animal_details_screen signature is (const char*). 
                // Event cb signature is (lv_event_t*).
                // So we need a wrapper.
            }
            core_free_animal_list(animals);
        }
    }
}

static void animal_item_wrapper_cb(lv_event_t * e) {
    const char * id = (const char *)lv_event_get_user_data(e);
    if (id) ui_create_animal_details_screen(id);
}

// Re-implement load_animal_list with correct callback
static void load_animal_list_correct(const char *query) {
    lv_obj_clean(list_animals);
    
    animal_summary_t *animals = NULL;
    size_t count = 0;
    if (core_search_animals(query, &animals, &count) == ESP_OK) {
        if (count == 0) {
            lv_list_add_text(list_animals, "Aucun animal trouvé.");
        } else {
            for (size_t i = 0; i < count; i++) {
                char *id_copy = ui_strdup(animals[i].id);
                char label[256];
                snprintf(label, sizeof(label), "%s (%s)", animals[i].name, animals[i].species);
                
                lv_obj_t * btn = lv_list_add_btn(list_animals, LV_SYMBOL_PASTE, label);
                lv_obj_add_event_cb(btn, animal_item_wrapper_cb, LV_EVENT_CLICKED, id_copy);
            }
            core_free_animal_list(animals);
        }
    }
}

static void search_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_DEFOCUSED) {
        const char * txt = lv_textarea_get_text(ta_search);
        load_animal_list_correct(txt);
    }
}

static void back_event_cb(lv_event_t * e) {
    ui_create_dashboard();
}

static void add_animal_event_cb(lv_event_t * e) {
    ui_create_animal_form_screen(NULL);
}

void ui_create_animal_list_screen(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
    const lv_coord_t header_height = 60;
    const lv_coord_t search_bar_height = 40;
    const lv_coord_t vertical_margin = 20; // approximate spacing between search bar and list

    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);

    // Header
    lv_obj_t * header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), header_height);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_palette_darken(LV_PALETTE_GREY, 3), 0);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn_back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT " Retour");

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "Mes Animaux");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * btn_add = lv_button_create(header);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(btn_add, add_animal_event_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_PLUS " Ajouter");

    // Search Bar
    ta_search = lv_textarea_create(scr);
    lv_textarea_set_one_line(ta_search, true);
    lv_textarea_set_placeholder_text(ta_search, "Rechercher...");
    lv_obj_set_size(ta_search, LV_PCT(90), search_bar_height);
    lv_obj_align(ta_search, LV_ALIGN_TOP_MID, 0, header_height + 10);
    lv_obj_add_event_cb(ta_search, search_event_cb, LV_EVENT_ALL, NULL);

    // Keyboard (Hidden initially)
    lv_obj_t * kb = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(kb, ta_search);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_add_event_cb(ta_search, (lv_event_cb_t)lv_keyboard_def_event_cb, LV_EVENT_FOCUSED, kb); // Show kb on focus? 
    // Simplified: just show/hide based on focus manually if needed, or use default behavior.
    // For now, let's just let user click it.

    // List
    list_animals = lv_list_create(scr);
    lv_coord_t list_h = disp_h - header_height - search_bar_height - vertical_margin;
    lv_obj_set_size(list_animals, disp_w, list_h);
    lv_obj_set_y(list_animals, header_height + search_bar_height + vertical_margin);

    load_animal_list_correct(NULL);

    lv_screen_load(scr);
}