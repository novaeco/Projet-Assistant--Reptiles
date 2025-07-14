#include "menu.h"
#include "keyboard.h"
#include "ui.h"

static uint16_t s_prev_keys;

void menu_process_keys(uint16_t keys)
{
    if (keys & 1) {
        ui_show_home();
    } else if (keys & 2) {
        ui_show_settings();
    } else if (keys & 4) {
        ui_show_network();
    } else if (keys & 8) {
        ui_show_images();
    }
}

void menu_update(void)
{
    uint16_t keys = keyboard_get_state();
    if (keys != s_prev_keys) {
        s_prev_keys = keys;
        menu_process_keys(keys);
    }
}
