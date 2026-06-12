#include "faculty175_usb.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "faculty175_storage.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "tusb_msc_storage.h"

static const char *TAG = "faculty175_usb";

#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"

static bool s_usb_ready;
static bool s_storage_ready;
static bool s_storage_mounted;

static bool path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    if (event == NULL) {
        return;
    }
    s_storage_mounted = event->mount_changed_data.is_mounted;
    ESP_LOGI(TAG, "usbflash mounted to app: %s", s_storage_mounted ? "yes" : "no");
}

esp_err_t faculty175_usb_init(void)
{
    if (s_usb_ready) {
        return ESP_OK;
    }

    const tinyusb_msc_spiflash_config_t msc_cfg = {
        .wl_handle = faculty175_storage_wl_handle(),
        .callback_mount_changed = storage_mount_changed_cb,
        .mount_config = {
            .format_if_mount_failed = true,
            .max_files = 8,
            .allocation_unit_size = 4096,
            .use_one_fat = false,
        },
    };
    ESP_RETURN_ON_ERROR(tinyusb_msc_storage_init_spiflash(&msc_cfg), TAG, "msc usbflash init");
    ESP_RETURN_ON_ERROR(tinyusb_msc_register_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED, storage_mount_changed_cb),
                        TAG,
                        "msc callback");

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .string_descriptor_count = 0,
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = NULL,
        .hs_configuration_descriptor = NULL,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = NULL,
#endif
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "tinyusb install");

    const tinyusb_config_cdcacm_t cdc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_RETURN_ON_ERROR(tusb_cdc_acm_init(&cdc_cfg), TAG, "cdc acm init");
    ESP_RETURN_ON_ERROR(esp_tusb_init_console(TINYUSB_CDC_ACM_0), TAG, "cdc console");

    s_storage_ready = true;
    s_usb_ready = true;
    ESP_LOGI(TAG,
             "TinyUSB CDC+MSC ready; usbflash=%s host=%s",
             s_storage_mounted ? "mounted" : "unmounted",
             tinyusb_msc_storage_in_use_by_usb_host() ? "yes" : "no");
    return ESP_OK;
}

bool faculty175_usb_storage_ready(void)
{
    return s_storage_ready;
}

esp_err_t faculty175_usb_storage_claim(void)
{
    if (!s_storage_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage_mounted) {
        return ESP_OK;
    }
    if (tinyusb_msc_storage_in_use_by_usb_host()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_storage_mounted = true;
        return ESP_OK;
    }
    return err;
}

bool faculty175_usb_storage_mounted(void)
{
    return s_storage_mounted;
}

const char *faculty175_usb_storage_base_path(void)
{
    return FACULTY175_USBFLASH_BASE_PATH;
}

bool faculty175_usb_storage_resolve_path(const char *relative_path, char *out, size_t cap)
{
    if (!s_storage_mounted || out == NULL || cap == 0 || relative_path == NULL || relative_path[0] == '\0') {
        return false;
    }

    const int n = snprintf(out, cap, "%s/%s", FACULTY175_USBFLASH_BASE_PATH, relative_path);
    return n > 0 && (size_t)n < cap && path_exists(out);
}
