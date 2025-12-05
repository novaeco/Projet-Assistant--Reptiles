#pragma once

#include "esp_err.h"
#include "esp_log.h"

#if __has_include("esp_check.h")
#include "esp_check.h"
#endif

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(expr, tag, format, ...)                   \
    do {                                                              \
        esp_err_t __err_rc = (expr);                                  \
        if (__err_rc != ESP_OK) {                                     \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                     \
            return __err_rc;                                          \
        }                                                             \
    } while (0)
#endif

#ifndef ESP_RETURN_ON_FALSE
#define ESP_RETURN_ON_FALSE(a, err_code, tag, format, ...)            \
    do {                                                              \
        if (!(a)) {                                                   \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                     \
            return (err_code);                                        \
        }                                                             \
    } while (0)
#endif

#ifndef ESP_GOTO_ON_ERROR
#define ESP_GOTO_ON_ERROR(expr, goto_tag, log_tag, format, ...)       \
    do {                                                              \
        ret = (expr);                                                 \
        if (ret != ESP_OK) {                                          \
            ESP_LOGE(log_tag, format, ##__VA_ARGS__);                 \
            goto goto_tag;                                            \
        }                                                             \
    } while (0)
#endif

#ifndef ESP_GOTO_ON_FALSE
#define ESP_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ...) \
    do {                                                              \
        if (!(a)) {                                                   \
            ret = (err_code);                                         \
            ESP_LOGE(log_tag, format, ##__VA_ARGS__);                 \
            goto goto_tag;                                            \
        }                                                             \
    } while (0)
#endif
