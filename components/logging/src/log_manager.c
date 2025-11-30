#include "logging.h"
#include "esp_log.h"

static const char *TAG = "LogManager";

esp_err_t logging_init(void) {
    ESP_LOGI(TAG, "Logging system initialized");
    return ESP_OK;
}