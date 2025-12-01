#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize IOT services (MQTT).
 * @return esp_err_t
 */
esp_err_t iot_init(void);

/**
 * @brief Publish system statistics to MQTT.
 */
void iot_mqtt_publish_stats(void);

/**
 * @brief Start Over-The-Air firmware update.
 *
 * @param url URL of the binary file (http/https).
 * @return esp_err_t
 */
esp_err_t iot_ota_start(const char *url);

#ifdef __cplusplus
}
#endif
