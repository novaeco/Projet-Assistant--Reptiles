#pragma once

/** Mount microSD card and make resources available */
void storage_sd_init(void);

/** Load a file into memory from the SD card */
void *storage_sd_load(const char *path, size_t *size);

