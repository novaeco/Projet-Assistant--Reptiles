#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "reptile_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "STORAGE";
static const char *NVS_NAMESPACE = "reptile_app";

esp_err_t storage_init(void)
{
    // NVS is typically initialized in app_main, but we can ensure it here or init specific namespaces
    // Assuming generic nvs_flash_init() is called in main.
    // We don't need to open a handle permanently, we'll open/close on demand for robustness.
    return ESP_OK;
}

// =============================================================================
// NVS Implementation
// =============================================================================

esp_err_t storage_nvs_set_i32(const char *key, int32_t value)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_i32(my_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t storage_nvs_get_i32(const char *key, int32_t *out_value)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_i32(my_handle, key, out_value);
    nvs_close(my_handle);
    return err;
}

esp_err_t storage_nvs_set_str(const char *key, const char *value)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t storage_nvs_get_str(const char *key, char *out_value, size_t max_len)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    size_t required_size = 0;
    err = nvs_get_str(my_handle, key, NULL, &required_size);
    if (err == ESP_OK) {
        if (required_size > max_len) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        } else {
            err = nvs_get_str(my_handle, key, out_value, &required_size);
            if (err == ESP_OK && max_len > 0) {
                out_value[max_len - 1] = '\0';
            }
        }
    }
    if (err != ESP_OK && max_len > 0) {
        out_value[0] = '\0';
    }
    nvs_close(my_handle);
    return err;
}

// =============================================================================
// File System Implementation
// =============================================================================

esp_err_t storage_file_write(const char *path, const char *data)
{
    ESP_LOGI(TAG, "Writing to file: %s", path);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, "%s", data);
    fclose(f);
    return ESP_OK;
}

char* storage_file_read(const char *path)
{
    ESP_LOGI(TAG, "Reading file: %s", path);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return NULL;
    }

    // Determine file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buffer = (char*)malloc(size + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file content");
        fclose(f);
        return NULL;
    }

    fread(buffer, 1, size, f);
    buffer[size] = '\0'; // Null-terminate
    fclose(f);

    return buffer;
}

esp_err_t storage_json_save(const char *filename, const cJSON *root)
{
    char *string = cJSON_Print(root);
    if (string == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        return ESP_FAIL;
    }

    esp_err_t ret = storage_file_write(filename, string);
    cJSON_free(string); // cJSON_Print allocates memory that must be freed
    return ret;
}

cJSON* storage_json_load(const char *filename)
{
    char *content = storage_file_read(filename);
    if (content == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(content);
    free(content); // Free the buffer from storage_file_read

    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON from %s", filename);
    }
    return root;
}