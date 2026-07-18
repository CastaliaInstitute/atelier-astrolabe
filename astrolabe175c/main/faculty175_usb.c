#include "faculty175_usb.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "faculty175_km.h"
#include "faculty175_storage.h"
#include "faculty175_usb_ncm.h"
#include "esp_private/periph_ctrl.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/soc_caps.h"
#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "tusb_msc_storage.h"
#endif

static const char *TAG = "faculty175_usb";

#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"
/*
 * Bench finding, 2026-07: this 1.75C exposes Espressif's fixed 303a:1001
 * Serial/JTAG device before TinyUSB starts. Once TinyUSB owns the PHY, macOS
 * sees neither a replacement NCM device nor the NCM host-init callback.
 * Keep the probe bounded so a missing OTG route cannot strand the board.
 */
#define FACULTY175_USB_NCM_HOST_TIMEOUT_MS 6000

static bool s_usb_initialized;
static bool s_screen_profile_active;
static bool s_cdc_active;
static bool s_cdc_console_active;
static bool s_storage_ready;
static bool s_storage_mounted;

static void restore_usb_serial_jtag(void)
{
    /* The console VFS talks directly to the fixed-function peripheral. Restore
     * its clock and pads without retaining internal-DMA driver ring buffers. */
#if !SOC_RCC_IS_INDEPENDENT
    PERIPH_RCC_ATOMIC() {
        usb_serial_jtag_ll_enable_bus_clock(true);
    }
#else
    usb_serial_jtag_ll_enable_bus_clock(true);
#endif
#if USB_SERIAL_JTAG_LL_EXT_PHY_SUPPORTED
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
#else
    usb_serial_jtag_ll_phy_set_defaults();
#endif
    ESP_LOGI(TAG, "USB Serial/JTAG restored");
}

static bool path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED
static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    if (event == NULL) {
        return;
    }
    s_storage_mounted = event->mount_changed_data.is_mounted;
    ESP_LOGI(TAG, "usbflash mounted to app: %s", s_storage_mounted ? "yes" : "no");
}
#endif

esp_err_t faculty175_usb_init(void)
{
#if !(CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED)
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_usb_initialized) {
        return ESP_OK;
    }

    /* Keep the fixed-function recovery console available while a blank or
     * damaged installer volume is mounted, formatted, and repaired. */
    restore_usb_serial_jtag();

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
    const esp_err_t msc_err = tinyusb_msc_storage_init_spiflash(&msc_cfg);
    if (msc_err == ESP_OK) {
        ESP_RETURN_ON_ERROR(tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH), TAG, "mount usbflash locally");
        const esp_err_t layout_err = faculty175_storage_prepare_media_layout();
        if (layout_err != ESP_OK) {
            ESP_LOGW(TAG, "usbflash payload unavailable: %s", esp_err_to_name(layout_err));
        }
        s_storage_ready = true;
    } else {
        ESP_LOGW(TAG, "MSC disabled: %s", esp_err_to_name(msc_err));
    }

    s_usb_initialized = true;
    if (faculty175_km_enabled()) {
        ESP_RETURN_ON_ERROR(faculty175_km_start(), TAG, "start WiFi KM");
    }
    ESP_LOGI(TAG, "USB Serial/JTAG profile ready; HID is gated by the usb-screen face");
    return ESP_OK;
#endif
}

esp_err_t faculty175_usb_set_screen_face_active(bool active)
{
#if !(CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED)
    return active ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
#else
    if (!s_usb_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (active == s_screen_profile_active) {
        return ESP_OK;
    }

    if (!active) {
        if (s_cdc_console_active) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_tusb_deinit_console(TINYUSB_CDC_ACM_0));
            s_cdc_console_active = false;
        }
        if (s_cdc_active) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0));
            s_cdc_active = false;
        }
        ESP_RETURN_ON_ERROR(tinyusb_driver_uninstall(), TAG, "disable screen USB profile");
        s_screen_profile_active = false;
        restore_usb_serial_jtag();
        if (s_storage_ready) {
            const esp_err_t mount_err = tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
            if (mount_err != ESP_OK) {
                ESP_LOGW(TAG, "usbflash local remount failed: %s", esp_err_to_name(mount_err));
            }
        }
        ESP_LOGI(TAG, "USB profile: Serial/JTAG (usb-screen inactive)");
        return ESP_OK;
    }

    if (s_storage_ready) {
        ESP_RETURN_ON_ERROR(tinyusb_msc_storage_unmount(), TAG, "expose usbflash to screen host");
    }

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = faculty175_km_enabled() ? faculty175_km_device_descriptor() : NULL,
        .string_descriptor = faculty175_km_enabled() ? faculty175_km_string_descriptors() : NULL,
        .string_descriptor_count = faculty175_km_enabled() ? (int)faculty175_km_string_descriptor_count() : 0,
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = faculty175_km_enabled() ? faculty175_km_configuration_descriptor() : NULL,
        .hs_configuration_descriptor = NULL,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = faculty175_km_enabled() ? faculty175_km_configuration_descriptor() : NULL,
#endif
    };
    const esp_err_t tinyusb_err = tinyusb_driver_install(&tusb_cfg);
    if (tinyusb_err != ESP_OK) {
        restore_usb_serial_jtag();
        if (s_storage_ready) {
            (void)tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
        }
        return tinyusb_err;
    }

#if CONFIG_TINYUSB_NET_MODE_NCM
    ESP_RETURN_ON_ERROR(faculty175_usb_ncm_init(), TAG, "ncm init");
    const esp_err_t ncm_host_err = faculty175_usb_ncm_wait_for_host(pdMS_TO_TICKS(FACULTY175_USB_NCM_HOST_TIMEOUT_MS));
    if (ncm_host_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "NCM host did not initialize within %u ms: %s; rolling back to Serial/JTAG",
                 FACULTY175_USB_NCM_HOST_TIMEOUT_MS,
                 esp_err_to_name(ncm_host_err));
        const esp_err_t uninstall_err = tinyusb_driver_uninstall();
        if (uninstall_err != ESP_OK) {
            ESP_LOGE(TAG, "TinyUSB teardown failed: %s", esp_err_to_name(uninstall_err));
        }
        restore_usb_serial_jtag();
        return ncm_host_err;
    }
#endif

    const tinyusb_config_cdcacm_t cdc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    const esp_err_t cdc_err = tusb_cdc_acm_init(&cdc_cfg);
    if (cdc_err == ESP_OK) {
        s_cdc_active = true;
        const esp_err_t console_err = esp_tusb_init_console(TINYUSB_CDC_ACM_0);
        if (console_err != ESP_OK) {
            ESP_LOGW(TAG, "CDC console disabled: %s", esp_err_to_name(console_err));
        } else {
            s_cdc_console_active = true;
        }
    } else {
        ESP_LOGW(TAG, "CDC disabled: %s", esp_err_to_name(cdc_err));
    }

    s_screen_profile_active = true;
    ESP_LOGI(TAG,
             "USB profile: TinyUSB CDC+MSC%s%s (usb-screen active); usbflash=%s host=%s",
             faculty175_usb_ncm_ready() ? "+NCM" : "",
             faculty175_km_enabled() ? "+HID keyboard+mouse" : "",
             s_storage_mounted ? "mounted" : "unmounted",
             tinyusb_msc_storage_in_use_by_usb_host() ? "yes" : "no");
    return ESP_OK;
#endif
}

bool faculty175_usb_screen_profile_active(void)
{
    return s_screen_profile_active;
}

bool faculty175_usb_storage_ready(void)
{
    return s_storage_ready;
}

esp_err_t faculty175_usb_storage_claim(void)
{
#if !(CONFIG_TINYUSB_MSC_ENABLED)
    return ESP_ERR_NOT_SUPPORTED;
#else
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
#endif
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
