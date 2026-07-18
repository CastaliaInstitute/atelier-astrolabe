#include "faculty175_storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "wear_levelling.h"

static const char *TAG = "faculty175_storage";

#define FACULTY175_USBFLASH_PARTITION "usbflash"
#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"
#define FACULTY175_MEDIA_BASE_PATH FACULTY175_USBFLASH_BASE_PATH "/media"

static wl_handle_t s_usbflash_wl = WL_INVALID_HANDLE;
static bool s_usbflash_ready;

#ifndef ASTROLABE_CYBER_FEATURES
#define ASTROLABE_CYBER_FEATURES 0
#endif

#if ASTROLABE_CYBER_FEATURES
extern const uint8_t astrolabe_install_sh_start[] asm("_binary_astrolabe_install_sh_start");
extern const uint8_t astrolabe_install_sh_end[] asm("_binary_astrolabe_install_sh_end");
extern const uint8_t astrolabe_pi_screen_py_start[] asm("_binary_astrolabe_pi_screen_py_start");
extern const uint8_t astrolabe_pi_screen_py_end[] asm("_binary_astrolabe_pi_screen_py_end");
#endif

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
    ESP_LOGW(TAG, "mkdir failed path=%s errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t write_embedded_file(const char *path, const uint8_t *start, const uint8_t *end)
{
    if (path == NULL || start == NULL || end == NULL || end <= start) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    size_t length = (size_t)(end - start);
    if (length > 0 && start[length - 1] == '\0') {
        --length;
    }
    const bool ok = fwrite(start, 1, length, file) == length && fflush(file) == 0;
    fclose(file);
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t ensure_media_layout(void)
{
    static const char *kDirs[] = {
        FACULTY175_USBFLASH_BASE_PATH "/update",
        FACULTY175_USBFLASH_BASE_PATH "/ASTROLABE",
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
#if ASTROLABE_CYBER_FEATURES
    ESP_RETURN_ON_ERROR(write_embedded_file(FACULTY175_USBFLASH_BASE_PATH "/ASTROLABE/astrolabe-install.sh",
                                             astrolabe_install_sh_start,
                                             astrolabe_install_sh_end),
                        TAG,
                        "write Pi installer");
    ESP_RETURN_ON_ERROR(write_embedded_file(FACULTY175_USBFLASH_BASE_PATH "/ASTROLABE/astrolabe_pi_screen.py",
                                             astrolabe_pi_screen_py_start,
                                             astrolabe_pi_screen_py_end),
                        TAG,
                        "write Pi screen agent");
#endif
    return ESP_OK;
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
    ESP_RETURN_ON_ERROR(wl_mount(partition, &s_usbflash_wl), TAG, "mount usbflash wear levelling");
    s_usbflash_ready = true;
    ESP_LOGI(TAG,
             "usbflash block device ready path=%s partition=%s wl=%" PRIi32,
             FACULTY175_USBFLASH_BASE_PATH,
             FACULTY175_USBFLASH_PARTITION,
             (int32_t)s_usbflash_wl);
    return ESP_OK;
}

esp_err_t faculty175_storage_prepare_media_layout(void)
{
    ESP_RETURN_ON_FALSE(s_usbflash_ready, ESP_ERR_INVALID_STATE, TAG, "usbflash block device not ready");
    return ensure_media_layout();
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
