#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the embedded web server.
 *
 * @return esp_err_t
 */
esp_err_t net_server_start(void);

/**
 * @brief Stop the embedded web server.
 *
 * @return esp_err_t
 */
esp_err_t net_server_stop(void);

#ifdef __cplusplus
}
#endif
