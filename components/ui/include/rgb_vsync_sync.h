#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_attr.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TaskHandle_t notify_task;
    volatile uint32_t vsync_count;
    volatile uint32_t pending_flush;
    volatile uint32_t last_swap_vsync;
    volatile bool registered;
    portMUX_TYPE spinlock;
} rgb_vsync_sync_t;

void rgb_vsync_sync_init(rgb_vsync_sync_t *sync, TaskHandle_t notify_task);
void rgb_vsync_sync_set_task(rgb_vsync_sync_t *sync, TaskHandle_t notify_task);
bool IRAM_ATTR rgb_vsync_sync_on_vsync(esp_lcd_panel_handle_t panel,
                                       const esp_lcd_rgb_panel_event_data_t *edata,
                                       void *user_ctx);
void rgb_vsync_sync_mark_flush(rgb_vsync_sync_t *sync, uint32_t *pending_before, uint32_t *current_vsync);
bool rgb_vsync_sync_wait(rgb_vsync_sync_t *sync, TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif

