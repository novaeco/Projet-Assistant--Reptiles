#include "ui.h"
#include "ui_lockscreen.h"
#include "board.h"
#include "board_pins.h"
#include "sdkconfig.h"
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "reptile_storage.h"
#include "esp_heap_caps.h"

static const char *TAG = "UI";

// LVGL Task Parameters
#define LVGL_TASK_STACK_SIZE 8192
#define LVGL_TASK_PRIORITY   5
#define LVGL_TICK_PERIOD_MS  2

static lv_display_t *s_disp = NULL;
static lv_indev_t *s_indev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static volatile uint32_t s_flush_count = 0;
typedef struct {
    lv_display_t *disp;
    volatile uint32_t flush_count;
    volatile uint32_t vsync_count;
    volatile uint32_t pending_flush;
    volatile uint32_t last_flush_vsync;
    volatile uint32_t last_swap_vsync;
    volatile uint32_t missed_swap_warning;
} ui_rgb_sync_t;

static ui_rgb_sync_t s_rgb_sync = {0};

static void ui_create_smoke_label(void);
static void ui_initial_invalidate_cb(lv_timer_t *timer);
static void ui_build_screens_async(void *arg);
#if CONFIG_UI_FORCE_HIGH_CONTRAST
static void ui_apply_high_contrast_screen(void);
#endif

// =============================================================================
// Flush Callback (Display)
// =============================================================================

static bool ui_on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    ui_rgb_sync_t *sync = (ui_rgb_sync_t *)user_ctx;
    if (!sync || !sync->disp) {
        return false;
    }

    uint32_t vsync = ++sync->vsync_count;
    if (sync->pending_flush) {
        sync->pending_flush = 0;
        sync->last_swap_vsync = vsync;
        lv_display_flush_ready(sync->disp);
    } else if ((vsync - sync->last_swap_vsync) > 240U) {
        sync->missed_swap_warning = 1;
    }

    return false;
}

static void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int x1 = area->x1;
    const int y1 = area->y1;
    const int x2 = area->x2 + 1;
    const int y2 = area->y2 + 1;

    uint32_t pending_before = s_rgb_sync.pending_flush;
    s_rgb_sync.pending_flush = 1;
    s_rgb_sync.last_flush_vsync = s_rgb_sync.vsync_count;
    uint32_t count = ++s_rgb_sync.flush_count;
    s_flush_count = count;

    esp_err_t draw_err = esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2, y2, px_map);
    if (count <= 5 || (count % 60U) == 0U) {
        ESP_LOGI(TAG, "LVGL flush #%u area x%d-%d y%d-%d vsync=%u pending=%u%s", (unsigned)count, x1, x2 - 1, y1,
                 y2 - 1, (unsigned)s_rgb_sync.vsync_count, (unsigned)s_rgb_sync.pending_flush,
                 (draw_err == ESP_OK) ? "" : " (draw err)");
    } else {
        ESP_LOGD(TAG, "flush_cb area x%d-%d y%d-%d (count=%u vsync=%u)%s", x1, x2 - 1, y1, y2 - 1,
                  (unsigned)count, (unsigned)s_rgb_sync.vsync_count, (draw_err == ESP_OK) ? "" : " draw_err");
    }

    if (draw_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(draw_err));
    }

    if (pending_before) {
        ESP_LOGW(TAG, "Flush overlap: previous buffer still pending at vsync=%u", (unsigned)s_rgb_sync.last_flush_vsync);
    }

    if (s_rgb_sync.missed_swap_warning) {
        s_rgb_sync.missed_swap_warning = 0;
        ESP_LOGW(TAG, "No buffer swap observed for >240 vsync ticks (vsync=%u flushes=%u)",
                 (unsigned)s_rgb_sync.vsync_count, (unsigned)s_rgb_sync.flush_count);
    }
}

// =============================================================================
// Read Callback (Touch)
// =============================================================================

static void ui_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (s_touch_handle) {
        uint16_t touch_x[1];
        uint16_t touch_y[1];
        uint8_t touch_cnt = 0;

        esp_lcd_touch_read_data(s_touch_handle);
        bool pressed = false;
        esp_lcd_touch_point_data_t point[1] = {0};
        esp_err_t err = esp_lcd_touch_get_data(s_touch_handle, point, &touch_cnt, 1);
        if (err == ESP_OK && touch_cnt > 0) {
            touch_x[0] = point[0].x;
            touch_y[0] = point[0].y;
            pressed = true;
        }

        if (pressed && touch_cnt > 0) {
            data->point.x = touch_x[0];
            data->point.y = touch_y[0];
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// =============================================================================
// Tick Timer
// =============================================================================

static void ui_tick_increment(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// =============================================================================
// LVGL Task
// =============================================================================

static void ui_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL Task Started");
    while (1) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms < 1) {
            wait_ms = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

// =============================================================================
// Initialization
// =============================================================================

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI...");

#if CONFIG_BOARD_LCD_STRIPES_TEST
    ESP_LOGW(TAG, "CONFIG_BOARD_LCD_STRIPES_TEST enabled: skipping LVGL init to keep RGB stripe pattern visible");
    return ESP_OK;
#endif

#if CONFIG_BOARD_LCD_BYPASS_LVGL_SELFTEST
    ESP_LOGW(TAG, "CONFIG_BOARD_LCD_BYPASS_LVGL_SELFTEST enabled: skipping LVGL bring-up");
    return ESP_OK;
#endif

    // 1. Initialize LVGL
    lv_init();

    // 2. Get Board Handles
    s_panel_handle = board_get_lcd_handle();
    s_touch_handle = board_get_touch_handle();

    if (s_panel_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD handle");
        return ESP_FAIL;
    }

    // 3. Create LVGL Display
    s_disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_user_data(s_disp, s_panel_handle);
    lv_display_set_flush_cb(s_disp, ui_flush_cb);
    s_rgb_sync.disp = s_disp;

    esp_lcd_rgb_panel_event_callbacks_t panel_cbs = {
        .on_vsync = ui_on_vsync,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(s_panel_handle, &panel_cbs, &s_rgb_sync));

    // 4. Set Buffers (Direct Mode with RGB Panel Buffers)
    void *buf1 = NULL;
    void *buf2 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel_handle, 2, &buf1, &buf2));

    // Use Direct Mode: LVGL renders directly into the frame buffer that is not active.
    lv_display_set_buffers(s_disp, buf1, buf2, BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_DIRECT);
    ESP_LOGI(TAG, "LVGL direct mode buffers: buf1=%p (%s), buf2=%p (%s)",
             buf1, esp_ptr_external_ram(buf1) ? "PSRAM" : "internal",
             buf2, esp_ptr_external_ram(buf2) ? "PSRAM" : "internal");
    ESP_LOGI(TAG, "LVGL display: hor_res=%d ver_res=%d color_depth=%d render_mode=%d", (int)lv_display_get_horizontal_resolution(s_disp),
             (int)lv_display_get_vertical_resolution(s_disp), LV_COLOR_DEPTH, (int)lv_display_get_render_mode(s_disp));

    // 6. Create Input Device (Touch)
    if (s_touch_handle) {
        s_indev = lv_indev_create();
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, ui_touch_read_cb);
    }

    // 7. Start Tick Timer early so async creation runs in LVGL context
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &ui_tick_increment,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // 8. Create LVGL Task
    xTaskCreatePinnedToCore(ui_task, "lvgl_task", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 1);

    // 9. Schedule UI construction asynchronously to avoid blocking app_main
#if CONFIG_UI_FORCE_HIGH_CONTRAST
    ui_apply_high_contrast_screen();
    ESP_LOGW(TAG, "CONFIG_UI_FORCE_HIGH_CONTRAST enabled: standard UI creation skipped");
    return ESP_OK;
#endif

    bool lockscreen_enabled = false;
    char pin[8] = {0};
    if (storage_nvs_get_str("sys_pin", pin, sizeof(pin)) == ESP_OK && strlen(pin) > 0) {
        lockscreen_enabled = true;
    }
    if (lv_async_call(ui_build_screens_async, (void *)(uintptr_t)lockscreen_enabled) != LV_RES_OK) {
        ESP_LOGW(TAG, "Failed to schedule async UI build");
    }

    return ESP_OK;
}

static void ui_create_smoke_label(void)
{
    if (!s_disp) {
        return;
    }
    lv_obj_t *smoke = lv_obj_create(lv_screen_active());
    lv_obj_set_size(smoke, lv_pct(60), lv_pct(20));
    lv_obj_center(smoke);
    lv_obj_set_style_bg_color(smoke, lv_color_hex(0x0A5BE0), 0);
    lv_obj_set_style_bg_opa(smoke, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(smoke, 0, 0);
    lv_obj_set_style_pad_all(smoke, 8, 0);

    lv_obj_t *label = lv_label_create(smoke);
    lv_label_set_text(label, "UI OK");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, lv_theme_get_font_large(label), 0);
    lv_obj_center(label);
    ESP_LOGI(TAG, "UI smoke label created on active screen");
}

#if CONFIG_UI_FORCE_HIGH_CONTRAST
static void ui_apply_high_contrast_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "BONJOUR");
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(label, lv_theme_get_font_large(label), 0);
    lv_obj_center(label);
    ESP_LOGI(TAG, "High-contrast fallback screen displayed");
}
#endif

static void ui_build_screens_async(void *arg)
{
    bool lockscreen_enabled = (bool)(uintptr_t)arg;
    if (lockscreen_enabled) {
        ui_create_lockscreen();
    } else {
        ui_create_dashboard();
    }

    ui_create_smoke_label();

    lv_timer_t *initial_invalidate = lv_timer_create(ui_initial_invalidate_cb, 100, NULL);
    if (initial_invalidate) {
        lv_timer_set_repeat_count(initial_invalidate, 1);
    } else {
        ESP_LOGW(TAG, "Failed to create initial invalidate timer");
    }

    ESP_LOGI(TAG, "UI async build complete (lockscreen=%d)", (int)lockscreen_enabled);
}

static void ui_initial_invalidate_cb(lv_timer_t *timer)
{
    lv_obj_t *screen = lv_screen_active();
    if (screen) {
        lv_obj_invalidate(screen);
        ESP_LOGI(TAG, "LVGL initial deferred invalidate queued (flush_count=%u)", (unsigned)s_flush_count);
    }
    if (timer) {
        lv_timer_del(timer);
    }
}
