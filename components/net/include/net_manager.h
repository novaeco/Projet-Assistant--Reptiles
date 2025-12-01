#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the network manager (WiFi, SNTP).
 *
 * @return esp_err_t
 */
esp_err_t net_init(void);

/**
 * @brief Connect to a WiFi network.
 *
 * @param ssid SSID of the network.
 * @param password Password of the network.
 * @return esp_err_t
 */
esp_err_t net_connect(const char *ssid, const char *password);

/**
 * @brief Check if connected to WiFi and got IP.
 *
 * @return true
 * @return false
 */
bool net_is_connected(void);

/**
 * @brief Perform a simple HTTP GET request.
 *
 * @param url Target URL.
 * @param out_buffer Buffer to store the response body.
 * @param buffer_len Size of the buffer.
 * @return esp_err_t
 */
esp_err_t net_http_get(const char *url, char *out_buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
