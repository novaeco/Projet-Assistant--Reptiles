#include "rgb_vsync_sync.h"

#include "freertos/semphr.h"

void rgb_vsync_sync_init(rgb_vsync_sync_t *sync, TaskHandle_t notify_task)
{
    if (!sync) {
        return;
    }
    sync->notify_task = notify_task;
    sync->vsync_count = 0;
    sync->pending_flush = 0;
    sync->last_swap_vsync = 0;
    sync->registered = false;
    sync->spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
}

void rgb_vsync_sync_set_task(rgb_vsync_sync_t *sync, TaskHandle_t notify_task)
{
    if (!sync) {
        return;
    }
    portENTER_CRITICAL(&sync->spinlock);
    sync->notify_task = notify_task;
    portEXIT_CRITICAL(&sync->spinlock);
}

bool IRAM_ATTR rgb_vsync_sync_on_vsync(esp_lcd_panel_handle_t panel,
                                       const esp_lcd_rgb_panel_event_data_t *edata,
                                       void *user_ctx)
{
    (void)panel;
    (void)edata;
    rgb_vsync_sync_t *sync = (rgb_vsync_sync_t *)user_ctx;
    if (!sync) {
        return false;
    }

    BaseType_t task_woken = pdFALSE;
    portENTER_CRITICAL_ISR(&sync->spinlock);
    uint32_t vsync = ++sync->vsync_count;
    if (sync->pending_flush > 0) {
        sync->pending_flush--;
        sync->last_swap_vsync = vsync;
        if (sync->notify_task) {
            vTaskNotifyGiveFromISR(sync->notify_task, &task_woken);
        }
    }
    portEXIT_CRITICAL_ISR(&sync->spinlock);
    return task_woken == pdTRUE;
}

void rgb_vsync_sync_mark_flush(rgb_vsync_sync_t *sync, uint32_t *pending_before, uint32_t *current_vsync)
{
    if (!sync) {
        return;
    }
    portENTER_CRITICAL(&sync->spinlock);
    if (pending_before) {
        *pending_before = sync->pending_flush;
    }
    sync->pending_flush++;
    if (current_vsync) {
        *current_vsync = sync->vsync_count;
    }
    portEXIT_CRITICAL(&sync->spinlock);
}

bool rgb_vsync_sync_wait(rgb_vsync_sync_t *sync, TickType_t timeout_ticks)
{
    if (!sync || !sync->notify_task) {
        portENTER_CRITICAL(&sync->spinlock);
        if (sync && sync->pending_flush) {
            sync->pending_flush--;
            sync->last_swap_vsync = sync->vsync_count;
        }
        portEXIT_CRITICAL(&sync->spinlock);
        return true;
    }

    uint32_t notified = ulTaskNotifyTake(pdTRUE, timeout_ticks);
    if (!notified) {
        portENTER_CRITICAL(&sync->spinlock);
        if (sync->pending_flush) {
            sync->pending_flush--;
        }
        portEXIT_CRITICAL(&sync->spinlock);
        return false;
    }
    return true;
}

