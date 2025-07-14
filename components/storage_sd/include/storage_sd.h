#pragma once

#include "esp_err.h"

/** Mount microSD card and make resources available */
esp_err_t storage_sd_init(void);

/** Load a file into memory from the SD card */
void *storage_sd_load(const char *path, size_t *size);

/** Unmount the microSD card */
esp_err_t storage_sd_unmount(void);

/** Poll card presence and log LVGL error if removed */
void storage_sd_update(void);

