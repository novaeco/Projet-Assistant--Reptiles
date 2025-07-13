#include "keyboard.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power.h"

#define TAG "keyboard"

#define KB_ROWS 4
#define KB_COLS 4

static const gpio_num_t row_pins[KB_ROWS] = {2, 3, 4, 5};
static const gpio_num_t col_pins[KB_COLS] = {6, 7, 8, 9};

static uint8_t debounce_cnt[KB_ROWS][KB_COLS];
static bool key_state[KB_ROWS][KB_COLS];
static uint16_t key_mask;
static uint16_t prev_mask;

static void keyboard_task(void *arg)
{
    while (1) {
        uint16_t mask = 0;
        for (int r = 0; r < KB_ROWS; r++) {
            gpio_set_level(row_pins[r], 0);
            ets_delay_us(5);
            for (int c = 0; c < KB_COLS; c++) {
                bool level = gpio_get_level(col_pins[c]) == 0;
                if (level == key_state[r][c]) {
                    if (debounce_cnt[r][c] < 5) debounce_cnt[r][c]++;
                } else {
                    debounce_cnt[r][c] = 0;
                }
                if (debounce_cnt[r][c] >= 5) {
                    key_state[r][c] = level;
                }
                if (key_state[r][c]) {
                    mask |= (1 << (r * KB_COLS + c));
                }
            }
            gpio_set_level(row_pins[r], 1);
        }

        /* simple anti-ghosting: clear if more than two keys in same column */
        for (int c = 0; c < KB_COLS; c++) {
            int pressed = 0;
            for (int r = 0; r < KB_ROWS; r++) {
                if (key_state[r][c]) pressed++;
            }
            if (pressed > 1) {
                for (int r = 0; r < KB_ROWS; r++) {
                    if (key_state[r][c]) {
                        mask &= ~(1 << (r * KB_COLS + c));
                    }
                }
            }
        }

        if (mask != prev_mask) {
            power_register_activity();
            prev_mask = mask;
        }
        key_mask = mask;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

uint16_t keyboard_get_state(void)
{
    return key_mask;
}

esp_err_t keyboard_init(void)
{
    for (int i = 0; i < KB_ROWS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << row_pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_config row %d failed: %s", i, esp_err_to_name(err));
            return err;
        }
        err = gpio_set_level(row_pins[i], 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_set_level row %d failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    for (int i = 0; i < KB_COLS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << col_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_config col %d failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    if (xTaskCreate(keyboard_task, "keyboard_task", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create keyboard task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Keyboard scanner started");
    return ESP_OK;
}

