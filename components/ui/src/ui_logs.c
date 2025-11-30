#include "ui_logs.h"
#include "ui.h"
#include "core_service.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void back_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ui_create_dashboard();
    }
}

static void refresh_btn_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ui_create_logs_screen(); // Reload
    }
}

void ui_create_logs_screen(void)
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
    lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT " Retour");

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "Journaux Systeme");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * btn_refresh = lv_button_create(header);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(btn_refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn_refresh), LV_SYMBOL_REFRESH);

    // 3. Log List
    lv_obj_t * list = lv_list_create(scr);
    lv_obj_set_size(list, LV_PCT(100), lv_pct(100) - 60);
    lv_obj_set_y(list, 60);
    
    // Use monospace font if possible, default otherwise
    lv_obj_set_style_text_font(list, LV_FONT_DEFAULT, 0); 

    char **logs = NULL;
    size_t count = 0;
    // Get last 50 logs
    if (core_get_logs(&logs, &count, 50) == ESP_OK) {
        if (count == 0) {
            lv_list_add_text(list, "Aucun journal.");
        } else {
            // Logs are returned oldest to newest usually (file order). 
            // Let's display newest on top -> iterate backwards
            for (int i = count - 1; i >= 0; i--) {
                // Parse the log line: Timestamp|Level|Module|Message
                // For display: "HH:MM [MOD] Message"
                
                char *line = logs[i];
                char *token_ts = strtok(line, "|");
                char *token_lvl = strtok(NULL, "|");
                char *token_mod = strtok(NULL, "|");
                char *token_msg = strtok(NULL, "|");

                if (token_ts && token_mod && token_msg) {
                    time_t ts = (time_t)atol(token_ts);
                    struct tm *tm_info = localtime(&ts);
                    char time_str[16];
                    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

                    char display_str[256];
                    snprintf(display_str, sizeof(display_str), "%s [%s] %s", time_str, token_mod, token_msg);
                    
                    lv_obj_t * btn = lv_list_add_btn(list, NULL, display_str);
                    // Color code based on level?
                    if (token_lvl && atoi(token_lvl) >= 2) { // Error
                        lv_obj_set_style_text_color(btn, lv_palette_main(LV_PALETTE_RED), 0);
                    }
                } else {
                    lv_list_add_btn(list, NULL, logs[i]); // Fallback
                }
            }
            core_free_log_list(logs, count);
        }
    } else {
        lv_list_add_text(list, "Erreur lecture logs.");
    }

    lv_screen_load(scr);
}