#include "faculty175_storage.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "faculty175_storage";

#define FACULTY175_USBFLASH_PARTITION "usbflash"
#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"

static wl_handle_t s_usbflash_wl = WL_INVALID_HANDLE;
static bool s_usbflash_ready;

static esp_err_t prepare_usbflash_fat(void)
{
    wl_handle_t probe_wl = WL_INVALID_HANDLE;
    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = 512,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(FACULTY175_USBFLASH_BASE_PATH,
                                                     FACULTY175_USBFLASH_PARTITION,
                                                     &mount_cfg,
                                                     &probe_wl);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_vfs_fat_spiflash_unmount_rw_wl(FACULTY175_USBFLASH_BASE_PATH, probe_wl);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "usbflash FAT prepared");
    }
    return err;
}

static bool path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

esp_err_t faculty175_storage_init(void)
{
    if (s_usbflash_ready) {
        return ESP_OK;
    }

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, FACULTY175_USBFLASH_PARTITION);
    ESP_RETURN_ON_FALSE(partition != NULL, ESP_ERR_NOT_FOUND, TAG, "usbflash partition missing");
    ESP_RETURN_ON_ERROR(prepare_usbflash_fat(), TAG, "prepare usbflash fat");
    ESP_RETURN_ON_ERROR(wl_mount(partition, &s_usbflash_wl), TAG, "wl mount usbflash");
    s_usbflash_ready = true;
    ESP_LOGI(TAG,
             "usbflash wl ready path=%s partition=%s",
             FACULTY175_USBFLASH_BASE_PATH,
             FACULTY175_USBFLASH_PARTITION);
    return ESP_OK;
}

bool faculty175_storage_ready(void)
{
    return s_usbflash_ready;
}

const char *faculty175_storage_base_path(void)
{
    return FACULTY175_USBFLASH_BASE_PATH;
}

wl_handle_t faculty175_storage_wl_handle(void)
{
    return s_usbflash_wl;
}

bool faculty175_storage_resolve_path(const char *relative_path, char *out, size_t cap)
{
    if (out == NULL || cap == 0 || relative_path == NULL || relative_path[0] == '\0' || !s_usbflash_ready) {
        return false;
    }

    const int n = snprintf(out, cap, "%s/%s", FACULTY175_USBFLASH_BASE_PATH, relative_path);
    return n > 0 && (size_t)n < cap && path_exists(out);
}
