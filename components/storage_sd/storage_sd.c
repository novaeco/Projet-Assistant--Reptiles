#include "storage_sd.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "ui.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "storage_sd"

static sdmmc_card_t *sdcard;
static bool card_missing;

esp_err_t storage_sd_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &sdcard);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SD card mounted");
    return ESP_OK;
}

void *storage_sd_load(const char *path, size_t *size)
{
    char full[128];
    snprintf(full, sizeof(full), "/sdcard/%s", path);
    FILE *f = fopen(full, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    void *buf = malloc(len);
    if (buf && fread(buf, 1, len, f) == len) {
        if (size) *size = len;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

esp_err_t storage_sd_unmount(void)
{
    if (!sdcard) {
        return ESP_OK;
    }
    esp_vfs_fat_sdmmc_unmount("/sdcard", sdcard);
    sdcard = NULL;
    ESP_LOGI(TAG, "SD card unmounted");
    return ESP_OK;
}

void storage_sd_update(void)
{
    DIR *d = opendir("/sdcard");
    if (!d) {
        if (!card_missing) {
            ESP_LOGE(TAG, "SD card removed");
            ui_show_error(ui_get_str(UI_STR_SD_REMOVED));
            storage_sd_unmount();
            card_missing = true;
        }
        return;
    }
    closedir(d);
}

