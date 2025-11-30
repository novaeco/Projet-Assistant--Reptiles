#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the storage system (NVS).
 *        Note: SD Card mounting is handled by the board component.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_init(void);

// =============================================================================
// NVS (Preferences)
// =============================================================================

/**
 * @brief Save an integer value to NVS.
 * 
 * @param key Key name (max 15 chars)
 * @param value Integer value
 * @return esp_err_t 
 */
esp_err_t storage_nvs_set_i32(const char *key, int32_t value);

/**
 * @brief Get an integer value from NVS.
 * 
 * @param key Key name
 * @param out_value Pointer to store the value
 * @return esp_err_t ESP_OK if found, ESP_ERR_NVS_NOT_FOUND if not found
 */
esp_err_t storage_nvs_get_i32(const char *key, int32_t *out_value);

/**
 * @brief Save a string to NVS.
 * 
 * @param key Key name
 * @param value String value
 * @return esp_err_t 
 */
esp_err_t storage_nvs_set_str(const char *key, const char *value);

/**
 * @brief Get a string from NVS.
 * 
 * @param key Key name
 * @param out_value Buffer to store the string
 * @param max_len Size of the buffer
 * @return esp_err_t 
 */
esp_err_t storage_nvs_get_str(const char *key, char *out_value, size_t max_len);

// =============================================================================
// File System (SD Card)
// =============================================================================

/**
 * @brief Write text data to a file.
 * 
 * @param path Full path (e.g., "/sdcard/data.txt")
 * @param data Null-terminated string to write
 * @return esp_err_t 
 */
esp_err_t storage_file_write(const char *path, const char *data);

/**
 * @brief Read text data from a file.
 *        Caller is responsible for freeing the returned pointer.
 * 
 * @param path Full path
 * @return char* Allocated string containing file content, or NULL on failure.
 */
char* storage_file_read(const char *path);

/**
 * @brief Save a cJSON object to a file.
 * 
 * @param filename Full path
 * @param root cJSON object
 * @return esp_err_t 
 */
esp_err_t storage_json_save(const char *filename, const cJSON *root);

/**
 * @brief Load a cJSON object from a file.
 *        Caller is responsible for deleting the returned cJSON object.
 * 
 * @param filename Full path
 * @return cJSON* Parsed JSON object or NULL on failure
 */
cJSON* storage_json_load(const char *filename);

#ifdef __cplusplus
}
#endif