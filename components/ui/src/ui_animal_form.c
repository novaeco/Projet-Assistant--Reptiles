#include "ui_animal_form.h"
#include "ui_animals.h"
#include "core_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t * ta_name;
static lv_obj_t * ta_species;
static lv_obj_t * dd_sex;
static lv_obj_t * ta_origin;
static lv_obj_t * ta_registry;
static lv_obj_t * kb;
static char current_animal_id[37];

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        /*Focus on the clicked text area*/
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

static void cancel_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ui_create_animal_list_screen();
    }
}

static void save_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        animal_t animal = {0};
        
        // ID: Keep existing if editing, empty if new (core will generate)
        strncpy(animal.id, current_animal_id, sizeof(animal.id));

        // Name
        const char * txt = lv_textarea_get_text(ta_name);
        strncpy(animal.name, txt, sizeof(animal.name) - 1);

        // Species
        txt = lv_textarea_get_text(ta_species);
        strncpy(animal.species, txt, sizeof(animal.species) - 1);

        // Sex
        uint16_t sex_idx = lv_dropdown_get_selected(dd_sex);
        // Options: "Inconnu\nMale\nFemelle" -> 0: U, 1: M, 2: F
        if (sex_idx == 1) animal.sex = SEX_MALE;
        else if (sex_idx == 2) animal.sex = SEX_FEMALE;
        else animal.sex = SEX_UNKNOWN;

        // Origin
        txt = lv_textarea_get_text(ta_origin);
        strncpy(animal.origin, txt, sizeof(animal.origin) - 1);

        // Registry
        txt = lv_textarea_get_text(ta_registry);
        strncpy(animal.registry_id, txt, sizeof(animal.registry_id) - 1);

        // Save
        esp_err_t err = core_save_animal(&animal);
        if (err == ESP_OK) {
            LV_LOG_USER("Animal saved successfully");
            ui_create_animal_list_screen();
        } else {
            LV_LOG_ERROR("Failed to save animal");
            // Optional: Show error message
        }
    }
}

void ui_create_animal_form_screen(const char *animal_id)
{
    lv_display_t *disp = lv_display_get_default();
    lv_coord_t disp_w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t disp_h = lv_display_get_vertical_resolution(disp);
    const lv_coord_t header_height = 60;

    // Init ID buffer
    memset(current_animal_id, 0, sizeof(current_animal_id));
    if (animal_id) {
        strncpy(current_animal_id, animal_id, sizeof(current_animal_id) - 1);
    }

    // 1. Screen
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);

    // 2. Header
    lv_obj_t * header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), header_height);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
    lv_obj_set_style_text_color(header, lv_color_white(), 0);

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, animal_id ? "Modifier Animal" : "Nouvel Animal");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 3. Container (Scrollable)
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, disp_w, disp_h - header_height); // Minus header
    lv_obj_set_y(cont, header_height);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 20, 0);
    lv_obj_set_style_pad_gap(cont, 15, 0);

    // 4. Fields
    // Name
    lv_obj_t * lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Nom / ID:");
    ta_name = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_name, true);
    lv_obj_set_width(ta_name, LV_PCT(100));
    lv_obj_add_event_cb(ta_name, ta_event_cb, LV_EVENT_ALL, NULL);

    // Species
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Espèce:");
    ta_species = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_species, true);
    lv_obj_set_width(ta_species, LV_PCT(100));
    lv_obj_add_event_cb(ta_species, ta_event_cb, LV_EVENT_ALL, NULL);

    // Sex
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Sexe:");
    dd_sex = lv_dropdown_create(cont);
    lv_dropdown_set_options(dd_sex, "Inconnu\nMale\nFemelle");
    lv_obj_set_width(dd_sex, LV_PCT(50));

    // Origin
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Origine (NC/WC/CB):");
    ta_origin = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_origin, true);
    lv_obj_set_width(ta_origin, LV_PCT(100));
    lv_obj_add_event_cb(ta_origin, ta_event_cb, LV_EVENT_ALL, NULL);

    // Registry ID
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Numéro I-FAP:");
    ta_registry = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta_registry, true);
    lv_obj_set_width(ta_registry, LV_PCT(100));
    lv_obj_add_event_cb(ta_registry, ta_event_cb, LV_EVENT_ALL, NULL);

    // 5. Buttons
    lv_obj_t * btn_cont = lv_obj_create(cont);
    lv_obj_set_size(btn_cont, LV_PCT(100), 80);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_cont, 20, 0);

    lv_obj_t * btn_cancel = lv_button_create(btn_cont);
    lv_obj_set_style_bg_color(btn_cancel, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btn_cancel, cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn_cancel);
    lv_label_set_text(lbl, "Annuler");

    lv_obj_t * btn_save = lv_button_create(btn_cont);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(btn_save, save_event_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn_save);
    lv_label_set_text(lbl, "Sauvegarder");

    // 6. Pre-fill data if editing
    if (animal_id) {
        animal_t animal;
        if (core_get_animal(animal_id, &animal) == ESP_OK) {
            lv_textarea_set_text(ta_name, animal.name);
            lv_textarea_set_text(ta_species, animal.species);
            lv_textarea_set_text(ta_origin, animal.origin);
            lv_textarea_set_text(ta_registry, animal.registry_id);
            
            if (animal.sex == SEX_MALE) lv_dropdown_set_selected(dd_sex, 1);
            else if (animal.sex == SEX_FEMALE) lv_dropdown_set_selected(dd_sex, 2);
            else lv_dropdown_set_selected(dd_sex, 0);
        }
    }

    // 7. Keyboard (Hidden by default)
    kb = lv_keyboard_create(scr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(scr);
}