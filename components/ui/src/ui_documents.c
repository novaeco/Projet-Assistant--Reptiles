#include "ui_documents.h"
#include "ui.h"
#include "core_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char * ui_strdup(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    if (dst) {
        memcpy(dst, src, len);
    }
    return dst;
}

static void back_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ui_create_dashboard();
    }
}

static void animal_select_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        const char * animal_id = (const char *)lv_event_get_user_data(e);
        if (animal_id) {
            if (core_generate_report(animal_id) == ESP_OK) {
                LV_LOG_USER("Report generated for %s", animal_id);
                // Reload documents screen
                ui_create_documents_screen();
            }
        }
        // Close modal (not implemented here, we just reload screen)
    }
}

static void generate_btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // Show list of animals to select
        // For simplicity in this MVP, we just pick the first animal or show a simple list
        // Here we create a simple modal-like list
        lv_obj_t * scr = lv_screen_active();
        
        lv_obj_t * mbox = lv_obj_create(scr);
        lv_obj_set_size(mbox, LV_PCT(80), LV_PCT(80));
        lv_obj_center(mbox);
        
        lv_obj_t * lbl = lv_label_create(mbox);
        lv_label_set_text(lbl, "Selectionner un animal:");
        
        lv_obj_t * list = lv_list_create(mbox);
        lv_obj_set_size(list, LV_PCT(100), LV_PCT(80));
        lv_obj_set_y(list, 30);
        
        animal_summary_t *animals = NULL;
        size_t count = 0;
        if (core_list_animals(&animals, &count) == ESP_OK) {
            for (size_t i = 0; i < count; i++) {
                char *id_copy = ui_strdup(animals[i].id); // Leak for MVP simplicity
                lv_obj_t * btn = lv_list_add_btn(list, NULL, animals[i].name);
                lv_obj_add_event_cb(btn, animal_select_event_cb, LV_EVENT_CLICKED, id_copy);
            }
            core_free_animal_list(animals);
        }
    }
}

void ui_create_documents_screen(void)
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
    lv_label_set_text(title, "Documents / Rapports");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 3. List of Reports
    lv_obj_t * list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), lv_pct(100) - 140); // Space for header and bottom button
    lv_obj_set_y(list, 60);

    char **reports = NULL;
    size_t count = 0;
    if (core_list_reports(&reports, &count) == ESP_OK) {
        if (count == 0) {
            lv_list_add_text(list, "Aucun rapport généré.");
        } else {
            for (size_t i = 0; i < count; i++) {
                lv_list_add_btn(list, LV_SYMBOL_FILE, reports[i]);
            }
            core_free_report_list(reports, count);
        }
    } else {
        lv_list_add_text(list, "Erreur lecture dossier.");
    }

    // 4. Generate Button
    lv_obj_t * btn_gen = lv_button_create(scr);
    lv_obj_set_size(btn_gen, 200, 50);
    lv_obj_align(btn_gen, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(btn_gen, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_gen, generate_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * lbl_gen = lv_label_create(btn_gen);
    lv_label_set_text(lbl_gen, "Générer Nouveau Rapport");
    lv_obj_center(lbl_gen);

    lv_screen_load(scr);
}