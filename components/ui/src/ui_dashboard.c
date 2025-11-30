#include "ui.h"
#include "ui_animals.h"
#include "ui_settings.h"
#include "ui_documents.h"
#include "ui_web.h"
#include "ui_logs.h"
#include "ui_alerts.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// =============================================================================
// Styles
// =============================================================================

static lv_style_t style_tile;
static lv_style_t style_header;
static lv_obj_t * clock_label;
static lv_timer_t * clock_timer;

static void init_styles(void)
{
    lv_style_init(&style_tile);
    lv_style_set_bg_color(&style_tile, lv_palette_main(LV_PALETTE_BLUE_GREY));
    lv_style_set_bg_opa(&style_tile, LV_OPA_COVER);
    lv_style_set_radius(&style_tile, 8);
    lv_style_set_text_color(&style_tile, lv_color_white());
    lv_style_set_pad_all(&style_tile, 10);

    lv_style_init(&style_header);
    lv_style_set_bg_color(&style_header, lv_palette_darken(LV_PALETTE_GREY, 3));
    lv_style_set_bg_opa(&style_header, LV_OPA_COVER);
    lv_style_set_text_color(&style_header, lv_color_white());
    lv_style_set_pad_all(&style_header, 10);
}

// =============================================================================
// Event Handlers
// =============================================================================

static void tile_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    const char * label_text = (const char *)lv_event_get_user_data(e);

    if(code == LV_EVENT_CLICKED) {
        LV_LOG_USER("Clicked: %s", label_text);
        
        if (strcmp(label_text, "Fiches Animaux") == 0) {
            ui_create_animal_list_screen();
        } else if (strcmp(label_text, "Paramètres") == 0) {
            ui_create_settings_screen();
        } else if (strcmp(label_text, "Documents") == 0) {
            ui_create_documents_screen();
        } else if (strcmp(label_text, "Requêtes Web") == 0) {
            ui_create_web_screen();
        } else if (strcmp(label_text, "Journaux") == 0) {
            ui_create_logs_screen();
        } else if (strcmp(label_text, "Alertes") == 0) {
            ui_create_alerts_screen();
        }
    }
}

static void clock_timer_cb(lv_timer_t * timer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%H:%M", &timeinfo);
    
    if (clock_label) {
        lv_label_set_text(clock_label, strftime_buf);
    }
}

// =============================================================================
// Helpers
// =============================================================================

static void create_tile(lv_obj_t * parent, const char * icon, const char * title, int col, int row, int w, int h, bool is_alert)
{
    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_add_style(btn, &style_tile, 0);
    
    if (is_alert) {
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_ORANGE), 0);
    }

    lv_obj_add_event_cb(btn, tile_event_cb, LV_EVENT_CLICKED, (void*)title);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%s\n%s", icon, title);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

// =============================================================================
// Dashboard Creation
// =============================================================================

void ui_create_dashboard(void)
{
    init_styles();

    // Create a new screen for the dashboard
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);

    // 1. Header
    lv_obj_t * header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_add_style(header, &style_header, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "Assistant Administratif Reptiles");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    clock_label = lv_label_create(header);
    lv_label_set_text(clock_label, "00:00");
    lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * wifi_icon = lv_label_create(header);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_icon, LV_ALIGN_RIGHT_MID, -10, 0);

    // Start Clock Timer
    if (clock_timer) lv_timer_del(clock_timer);
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    clock_timer_cb(clock_timer); // Update immediately

    // 2. Grid Container for Tiles
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t * grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_y(grid, 60); // Below header
    lv_obj_set_height(grid, lv_pct(100) - 60); // Remaining height (approx)
    lv_obj_set_size(grid, 1024, 600 - 60);
    
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_pad_all(grid, 20);
    lv_obj_set_style_grid_column_dsc_array(grid, col_dsc, 0);
    lv_obj_set_style_grid_row_dsc_array(grid, row_dsc, 0);
    lv_obj_set_style_grid_row_gap(grid, 20, 0);
    lv_obj_set_style_grid_column_gap(grid, 20, 0);

    // 3. Create Tiles
    // Row 0
    create_tile(grid, LV_SYMBOL_LIST, "Fiches Animaux", 0, 0, 1, 1, false);
    create_tile(grid, LV_SYMBOL_FILE, "Documents", 1, 0, 1, 1, false);
    create_tile(grid, LV_SYMBOL_GLOBE, "Requêtes Web", 2, 0, 1, 1, false);

    // Row 1
    create_tile(grid, LV_SYMBOL_EDIT, "Journaux", 0, 1, 1, 1, false);
    create_tile(grid, LV_SYMBOL_SETTINGS, "Paramètres", 1, 1, 1, 1, false);
    create_tile(grid, LV_SYMBOL_WARNING, "Alertes", 2, 1, 1, 1, true); // Replaced Aide with Alertes

    // Load the screen
    lv_screen_load(scr);
}