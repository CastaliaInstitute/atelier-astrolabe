#include "faculty175_storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "faculty175_storage";

#define FACULTY175_USBFLASH_PARTITION "usbflash"
#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"
#define FACULTY175_MEDIA_BASE_PATH FACULTY175_USBFLASH_BASE_PATH "/media"

static wl_handle_t s_usbflash_wl = WL_INVALID_HANDLE;
static bool s_usbflash_ready;

static bool dir_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static esp_err_t ensure_dir(const char *path)
{
    if (dir_exists(path)) {
        return ESP_OK;
    }
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t ensure_media_layout(void)
{
    static const char *kDirs[] = {
        FACULTY175_USBFLASH_BASE_PATH "/update",
        FACULTY175_MEDIA_BASE_PATH,
        FACULTY175_MEDIA_BASE_PATH "/backgrounds",
        FACULTY175_MEDIA_BASE_PATH "/bust_cache",
        FACULTY175_MEDIA_BASE_PATH "/tarot",
        FACULTY175_MEDIA_BASE_PATH "/tarot/deck",
        FACULTY175_MEDIA_BASE_PATH "/tarot/deck/466",
    };

    for (size_t i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]); ++i) {
        esp_err_t err = ensure_dir(kDirs[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mkdir failed: %s", kDirs[i]);
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t mount_usbflash_fat(void)
{
    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = 512,
        .use_one_fat = false,
    };

    return esp_vfs_fat_spiflash_mount_rw_wl(FACULTY175_USBFLASH_BASE_PATH,
                                            FACULTY175_USBFLASH_PARTITION,
                                            &mount_cfg,
                                            &s_usbflash_wl);
}

static esp_err_t format_usbflash_fat(void)
{
    esp_vfs_fat_mount_config_t format_cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = 512,
        .use_one_fat = false,
    };
    return esp_vfs_fat_spiflash_format_cfg_rw_wl(FACULTY175_USBFLASH_BASE_PATH,
                                                 FACULTY175_USBFLASH_PARTITION,
                                                 &format_cfg);
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
#if ASTROLABE_USB_OTA_DEMO_BOOT
    ESP_LOGI(TAG, "usbflash demo boot format");
    ESP_RETURN_ON_ERROR(format_usbflash_fat(), TAG, "format usbflash fat");
#endif
    ESP_RETURN_ON_ERROR(mount_usbflash_fat(), TAG, "mount usbflash fat");
    ESP_RETURN_ON_ERROR(ensure_media_layout(), TAG, "usbflash media layout");
    s_usbflash_ready = true;
    ESP_LOGI(TAG,
             "usbflash FAT ready path=%s partition=%s wl=%" PRIi32,
             FACULTY175_USBFLASH_BASE_PATH,
             FACULTY175_USBFLASH_PARTITION,
             (int32_t)s_usbflash_wl);
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

const char *faculty175_storage_media_base_path(void)
{
    return FACULTY175_MEDIA_BASE_PATH;
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

bool faculty175_storage_media_resolve_path(const char *relative_path, char *out, size_t cap)
{
    if (out == NULL || cap == 0 || relative_path == NULL || relative_path[0] == '\0' || !s_usbflash_ready) {
        return false;
    }

    const int n = snprintf(out, cap, "%s/%s", FACULTY175_MEDIA_BASE_PATH, relative_path);
    return n > 0 && (size_t)n < cap && path_exists(out);
}
