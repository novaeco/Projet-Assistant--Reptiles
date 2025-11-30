#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Export all animals to a CSV file on SD card.
 * 
 * @param filename Output filename (e.g., "/sdcard/export.csv")
 * @return esp_err_t 
 */
esp_err_t core_export_csv(const char *filename);

#ifdef __cplusplus
}
#endif