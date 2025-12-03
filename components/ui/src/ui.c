#include "ui.h"
#include "ui_lockscreen.h"
#include "board.h"
#include "board_pins.h"
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
static int64_t s_flush_deadline_us = 0;

// =============================================================================
// Flush Callback (Display)
// =============================================================================

// Callback from the RGB panel driver when a frame transfer is done
static volatile bool s_vsync_fired = false;
static volatile bool s_flush_waiting = false;
static bool s_use_vsync = false;

static bool IRAM_ATTR on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    (void)panel;
    (void)event_data;
    (void)user_data;
    s_vsync_fired = true;
    return false;
}

static void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // For RGB interface in Direct Mode, we just need to pass the buffer to the driver
    // The driver handles the timing.
    // Note: x2 and y2 in LVGL are inclusive, esp_lcd expects exclusive upper bound? 
    // Check esp_lcd_panel_draw_bitmap docs: x_end, y_end are exclusive.
    // But for RGB panel, it usually takes the whole buffer or just swaps.
    
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;

    esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2, y2, px_map);
    s_vsync_fired = false;
    s_flush_waiting = true;
    // Set a safety deadline in case VSYNC never triggers (avoid WDT lockup)
    s_flush_deadline_us = esp_timer_get_time() + 20000; // 20ms deadline

    // If vsync callback registration failed, complete immediately
    if (!s_use_vsync) {
        s_flush_waiting = false;
        lv_display_flush_ready(disp);
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
        uint32_t time_till_next = lv_timer_handler();

        if (s_flush_waiting) {
            bool vsync_ok = s_use_vsync && s_vsync_fired;
            bool timeout = esp_timer_get_time() >= s_flush_deadline_us;
            if (vsync_ok || timeout) {
                s_vsync_fired = false;
                s_flush_waiting = false;
                lv_display_flush_ready(s_disp);
            }
        }

        uint32_t sleep_ms = time_till_next > 5 ? time_till_next : 5;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

// =============================================================================
// Initialization
// =============================================================================

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI...");

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

    // 4. Register VSync Callback for Flush Ready
    // We register it AFTER creating s_disp so we can pass it as user_data
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_vsync = on_vsync_event,
    };
#ifdef CONFIG_LCD_RGB_ISR_IRAM_SAFE
    esp_err_t cb_err = esp_lcd_rgb_panel_register_event_callbacks(s_panel_handle, &callbacks, s_disp);
    if (cb_err != ESP_OK) {
        ESP_LOGW(TAG, "VSync callback registration failed: %s (continuing without IRAM callback)", esp_err_to_name(cb_err));
        s_use_vsync = false;
    } else {
        s_use_vsync = true;
    }
#else
    ESP_LOGW(TAG, "RGB ISR IRAM safety disabled in sdkconfig; skipping vsync callback registration.");
    s_use_vsync = false;
#endif

    // 5. Set Buffers (Direct Mode with RGB Panel Buffers)
    void *buf1 = NULL;
    void *buf2 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel_handle, 2, &buf1, &buf2));

    // Use Direct Mode: LVGL renders directly into the frame buffer that is not active.
    lv_display_set_buffers(s_disp, buf1, buf2, BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_DIRECT);
    ESP_LOGI(TAG, "LVGL direct mode buffers: buf1=%p (%s), buf2=%p (%s)",
             buf1, esp_ptr_external_ram(buf1) ? "PSRAM" : "internal",
             buf2, esp_ptr_external_ram(buf2) ? "PSRAM" : "internal");

    // 6. Create Input Device (Touch)
    if (s_touch_handle) {
        s_indev = lv_indev_create();
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, ui_touch_read_cb);
    }

    // 7. Check for PIN and decide start screen
    char pin[8] = {0};
    if (storage_nvs_get_str("sys_pin", pin, sizeof(pin)) == ESP_OK && strlen(pin) > 0) {
        ui_create_lockscreen();
    } else {
        ui_create_dashboard();
    }

    // 8. Start Tick Timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &ui_tick_increment,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // 9. Create LVGL Task
    xTaskCreatePinnedToCore(ui_task, "lvgl_task", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 1);

    return ESP_OK;
}