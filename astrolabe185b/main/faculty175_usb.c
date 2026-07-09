#include "faculty175_usb.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "faculty175_storage.h"
#include "sdmmc_cmd.h"
#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "tusb_msc_storage.h"
#endif

static const char *TAG = "faculty175_usb";

#define FACULTY175_SD_BASE_PATH "/sdcard"
#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"
#define FACULTY175_SD_PIN_CLK GPIO_NUM_15
#define FACULTY175_SD_PIN_CMD GPIO_NUM_14
#define FACULTY175_SD_PIN_D0 GPIO_NUM_16
#define FACULTY175_SD_PIN_D1 GPIO_NUM_17
#define FACULTY175_SD_PIN_D2 GPIO_NUM_12
#define FACULTY175_SD_PIN_D3 GPIO_NUM_13

static sdmmc_card_t *s_sd_card;
static bool s_usb_ready;
static bool s_storage_ready;
static bool s_storage_mounted;
static bool s_sd_mounted;

#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED
static void storage_mount_changed_cb(tinyusb_msc_event_t *event);

static void sync_usbflash_mount_for_owner(bool host_mounted)
{
    if (host_mounted) {
        if (!s_storage_mounted) {
            return;
        }
        const esp_err_t err = tinyusb_msc_storage_unmount();
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_storage_mounted = false;
        } else {
            ESP_LOGW(TAG, "usbflash unmount failed: %s", esp_err_to_name(err));
        }
        return;
    }

    if (s_storage_mounted) {
        return;
    }
    const esp_err_t err = tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_storage_mounted = true;
    } else {
        ESP_LOGW(TAG, "usbflash mount failed: %s", esp_err_to_name(err));
    }
}

static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    if (event == NULL) {
        return;
    }
    const bool host_mounted = event->mount_changed_data.is_mounted;
    ESP_LOGI(TAG, "usbflash host mounted: %s", host_mounted ? "yes" : "no");
    sync_usbflash_mount_for_owner(host_mounted);
    ESP_LOGI(TAG, "usbflash mounted to app: %s", s_storage_mounted ? "yes" : "no");
}
#endif

static bool path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static esp_err_t storage_init_sdmmc(void)
{
    if (s_sd_mounted) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = FACULTY175_SD_PIN_CLK;
    slot_config.cmd = FACULTY175_SD_PIN_CMD;
    slot_config.d0 = FACULTY175_SD_PIN_D0;
    slot_config.d1 = FACULTY175_SD_PIN_D1;
    slot_config.d2 = FACULTY175_SD_PIN_D2;
    slot_config.d3 = FACULTY175_SD_PIN_D3;
#endif
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(FACULTY175_SD_BASE_PATH, &host, &slot_config, &mount_cfg, &s_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sdcard mount failed: %s", esp_err_to_name(err));
        s_sd_card = NULL;
        return err;
    }
    s_sd_mounted = true;
    sdmmc_card_print_info(stdout, s_sd_card);
    return ESP_OK;
}

esp_err_t faculty175_usb_init(void)
{
    if (s_usb_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "usb init begin");
    (void)storage_init_sdmmc();

#if !(CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED)
    s_storage_ready = faculty175_storage_ready();
    s_storage_mounted = s_storage_ready;
    s_usb_ready = true;
    ESP_LOGI(TAG,
             "development USB path active; TinyUSB CDC/MSC disabled, sd=%s usbflash=%s",
             s_sd_mounted ? "mounted" : "missing",
             s_storage_ready ? "ready" : "missing");
    return ESP_OK;
#else

    const tinyusb_msc_spiflash_config_t msc_cfg = {
        .wl_handle = faculty175_storage_wl_handle(),
        .callback_mount_changed = storage_mount_changed_cb,
        .mount_config = {
            .format_if_mount_failed = true,
            .max_files = 8,
            .allocation_unit_size = 512,
            .use_one_fat = false,
        },
    };
    ESP_LOGI(TAG, "init msc spiflash");
    ESP_RETURN_ON_ERROR(tinyusb_msc_storage_init_spiflash(&msc_cfg), TAG, "msc usbflash init");
    ESP_LOGI(TAG, "register msc callback");
    ESP_RETURN_ON_ERROR(tinyusb_msc_register_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED, storage_mount_changed_cb),
                        TAG,
                        "msc callback");

    sync_usbflash_mount_for_owner(tinyusb_msc_storage_in_use_by_usb_host());

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
    ESP_LOGI(TAG, "install tinyusb driver");
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
    ESP_LOGI(TAG, "init cdc acm");
    ESP_RETURN_ON_ERROR(tusb_cdc_acm_init(&cdc_cfg), TAG, "cdc acm init");
    ESP_LOGI(TAG, "switch console to cdc");
    ESP_RETURN_ON_ERROR(esp_tusb_init_console(TINYUSB_CDC_ACM_0), TAG, "cdc console");

    s_storage_ready = true;
    s_usb_ready = true;
    ESP_LOGI(TAG,
             "TinyUSB CDC+MSC ready; usbflash=%s host=%s sd=%s",
             s_storage_mounted ? "mounted" : "unmounted",
             tinyusb_msc_storage_in_use_by_usb_host() ? "yes" : "no",
             s_sd_mounted ? "mounted" : "missing");
    return ESP_OK;
#endif
}

bool faculty175_usb_storage_ready(void)
{
    return s_storage_ready;
}

bool faculty175_usb_storage_mounted(void)
{
    return s_storage_mounted;
}

const char *faculty175_usb_storage_base_path(void)
{
    return FACULTY175_SD_BASE_PATH;
}

bool faculty175_usb_resolve_asset_path(const char *sd_relative, const char *fallback_path, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return false;
    }
    out[0] = '\0';

    if (sd_relative != NULL && sd_relative[0] != '\0' && s_sd_mounted) {
        const int n = snprintf(out, cap, "%s/%s", FACULTY175_SD_BASE_PATH, sd_relative);
        if (n > 0 && (size_t)n < cap && path_exists(out)) {
            return true;
        }
    }

    if (faculty175_storage_resolve_path(sd_relative, out, cap)) {
        return true;
    }

    if (fallback_path != NULL && fallback_path[0] != '\0' && path_exists(fallback_path)) {
        const int n = snprintf(out, cap, "%s", fallback_path);
        return n > 0 && (size_t)n < cap;
    }
    return false;
}
