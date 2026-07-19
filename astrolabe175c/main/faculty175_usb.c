#include "faculty175_usb.h"

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "faculty175_km.h"
#include "faculty175_storage.h"
#include "faculty175_usb_ncm.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_private/periph_ctrl.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/soc_caps.h"
#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_NET_MODE_NCM
#include "tinyusb.h"
#if CONFIG_TINYUSB_CDC_ENABLED
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#endif
#if CONFIG_TINYUSB_MSC_ENABLED
#include "tusb_msc_storage.h"
#endif
#endif

static const char *TAG = "faculty175_usb";

#define FACULTY175_USBFLASH_BASE_PATH "/usbflash"
#define FACULTY175_USB_NVS_NS "usb_cfg"
#define FACULTY175_USB_AUTO_TETHER_KEY "auto_tether"
#define FACULTY175_USB_TETHER_DIAG_MAGIC 0x175c5553u
/*
 * Bench finding, 2026-07: this 1.75C exposes Espressif's fixed 303a:1001
 * Serial/JTAG device before TinyUSB starts. Once TinyUSB owns the PHY, macOS
 * sees neither a replacement NCM device nor the NCM host-init callback.
 * Keep the probe bounded so a missing OTG route cannot strand the board.
 */
#define FACULTY175_USB_NCM_HOST_TIMEOUT_MS 6000

static bool s_usb_initialized;
static bool s_screen_profile_active;
#if CONFIG_TINYUSB_CDC_ENABLED
static bool s_cdc_active;
static bool s_cdc_console_active;
#endif
static bool s_storage_ready;
static bool s_storage_mounted;
#if CONFIG_TINYUSB_MSC_ENABLED
static bool s_storage_exposed_to_host;
#endif
static bool s_tether_activation_pending;
static bool s_tether_mode_active;
static bool s_auto_tether_loaded;
static bool s_auto_tether_enabled;

typedef enum {
    FACULTY175_USB_TETHER_STAGE_IDLE = 0,
    FACULTY175_USB_TETHER_STAGE_REQUESTED,
    FACULTY175_USB_TETHER_STAGE_ACTIVATE,
    FACULTY175_USB_TETHER_STAGE_RELEASE_SERIAL,
    FACULTY175_USB_TETHER_STAGE_TINYUSB_INSTALL,
    FACULTY175_USB_TETHER_STAGE_NCM_INIT,
    FACULTY175_USB_TETHER_STAGE_WAIT_HOST,
    FACULTY175_USB_TETHER_STAGE_HTTP_READY,
    FACULTY175_USB_TETHER_STAGE_ROLLBACK,
    FACULTY175_USB_TETHER_STAGE_READY,
} faculty175_usb_tether_stage_t;

typedef struct {
    uint32_t magic;
    uint32_t attempts;
    uint32_t last_uptime_ms;
    int32_t last_stage;
    int32_t last_error;
} faculty175_usb_tether_diag_t;

static RTC_NOINIT_ATTR faculty175_usb_tether_diag_t s_tether_diag;

static const char *tether_stage_name(faculty175_usb_tether_stage_t stage)
{
    switch (stage) {
    case FACULTY175_USB_TETHER_STAGE_IDLE:
        return "idle";
    case FACULTY175_USB_TETHER_STAGE_REQUESTED:
        return "requested";
    case FACULTY175_USB_TETHER_STAGE_ACTIVATE:
        return "activate";
    case FACULTY175_USB_TETHER_STAGE_RELEASE_SERIAL:
        return "release-serial";
    case FACULTY175_USB_TETHER_STAGE_TINYUSB_INSTALL:
        return "tinyusb-install";
    case FACULTY175_USB_TETHER_STAGE_NCM_INIT:
        return "ncm-init";
    case FACULTY175_USB_TETHER_STAGE_WAIT_HOST:
        return "wait-host";
    case FACULTY175_USB_TETHER_STAGE_HTTP_READY:
        return "http-ready";
    case FACULTY175_USB_TETHER_STAGE_ROLLBACK:
        return "rollback";
    case FACULTY175_USB_TETHER_STAGE_READY:
        return "ready";
    default:
        return "unknown";
    }
}

static void tether_diag_init_once(void)
{
    if (s_tether_diag.magic == FACULTY175_USB_TETHER_DIAG_MAGIC) {
        return;
    }
    s_tether_diag.magic = FACULTY175_USB_TETHER_DIAG_MAGIC;
    s_tether_diag.attempts = 0;
    s_tether_diag.last_uptime_ms = 0;
    s_tether_diag.last_stage = FACULTY175_USB_TETHER_STAGE_IDLE;
    s_tether_diag.last_error = ESP_OK;
}

static void tether_diag_mark(faculty175_usb_tether_stage_t stage, esp_err_t err)
{
    tether_diag_init_once();
    s_tether_diag.last_stage = stage;
    s_tether_diag.last_error = err;
    s_tether_diag.last_uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

static void tether_diag_attempt(void)
{
    tether_diag_init_once();
    ++s_tether_diag.attempts;
    tether_diag_mark(FACULTY175_USB_TETHER_STAGE_REQUESTED, ESP_OK);
}

static void release_usb_serial_jtag_driver(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_use_nonblocking();
    const esp_err_t err = usb_serial_jtag_driver_uninstall();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "USB Serial/JTAG release failed: %s", esp_err_to_name(err));
    }
#endif
}

static bool usb_auto_tether_default(void)
{
#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
    return true;
#else
    return false;
#endif
}

static void usb_auto_tether_load_once(void)
{
    if (s_auto_tether_loaded) {
        return;
    }
    s_auto_tether_enabled = usb_auto_tether_default();
    nvs_handle_t nvs;
    if (nvs_open(FACULTY175_USB_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t value = s_auto_tether_enabled ? 1 : 0;
        if (nvs_get_u8(nvs, FACULTY175_USB_AUTO_TETHER_KEY, &value) == ESP_OK) {
            s_auto_tether_enabled = value != 0;
        }
        nvs_close(nvs);
    }
    s_auto_tether_loaded = true;
}

static void restore_usb_serial_jtag(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    const bool reinstall_driver = s_usb_initialized;
#endif
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
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    if (reinstall_driver) {
        usb_serial_jtag_driver_config_t usb_serial_config = {
            .tx_buffer_size = 1024,
            .rx_buffer_size = 1024,
        };
        const esp_err_t err = usb_serial_jtag_driver_install(&usb_serial_config);
        if (err == ESP_OK) {
            usb_serial_jtag_vfs_use_driver();
        } else if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "USB Serial/JTAG driver restore failed: %s", esp_err_to_name(err));
        }
    }
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

#if CONFIG_TINYUSB_MSC_ENABLED
static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    if (event == NULL) {
        return;
    }
    s_storage_mounted = event->mount_changed_data.is_mounted;
    ESP_LOGI(TAG, "usbflash mounted to app: %s", s_storage_mounted ? "yes" : "no");
}
#endif

static bool storage_host_in_use(void)
{
#if CONFIG_TINYUSB_MSC_ENABLED
    return tinyusb_msc_storage_in_use_by_usb_host();
#else
    return false;
#endif
}

esp_err_t faculty175_usb_init(void)
{
#if !(CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_NET_MODE_NCM)
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_usb_initialized) {
        return ESP_OK;
    }
    tether_diag_init_once();
    usb_auto_tether_load_once();

    /* Keep the fixed-function recovery console available while a blank or
     * damaged installer volume is mounted, formatted, and repaired. */
    restore_usb_serial_jtag();

#if CONFIG_TINYUSB_MSC_ENABLED
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
#endif

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
#if !(CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_NET_MODE_NCM)
    return active ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
#else
    if (!s_usb_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (active == s_screen_profile_active) {
        return ESP_OK;
    }

    if (!active) {
        s_tether_mode_active = false;
#if CONFIG_TINYUSB_CDC_ENABLED
        if (s_cdc_console_active) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_tusb_deinit_console(TINYUSB_CDC_ACM_0));
            s_cdc_console_active = false;
        }
        if (s_cdc_active) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0));
            s_cdc_active = false;
        }
#endif
        ESP_RETURN_ON_ERROR(tinyusb_driver_uninstall(), TAG, "disable screen USB profile");
        s_screen_profile_active = false;
        restore_usb_serial_jtag();
#if CONFIG_TINYUSB_MSC_ENABLED
        if (s_storage_ready) {
            if (s_storage_exposed_to_host || !s_storage_mounted) {
                const esp_err_t mount_err = tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
                if (mount_err != ESP_OK) {
                    ESP_LOGW(TAG, "usbflash local remount failed: %s", esp_err_to_name(mount_err));
                }
            }
            s_storage_exposed_to_host = false;
        }
#endif
        ESP_LOGI(TAG, "USB profile: Serial/JTAG (usb-screen inactive)");
        return ESP_OK;
    }

    const bool tether_activation = s_tether_activation_pending;
    s_tether_activation_pending = false;
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_ACTIVATE, ESP_OK);
    }
#if !CONFIG_TINYUSB_MSC_ENABLED
    (void)tether_activation;
#endif
#if CONFIG_TINYUSB_MSC_ENABLED
    if (s_storage_ready && !tether_activation) {
        ESP_RETURN_ON_ERROR(tinyusb_msc_storage_unmount(), TAG, "expose usbflash to screen host");
        s_storage_exposed_to_host = true;
    } else if (s_storage_ready && tether_activation) {
        ESP_LOGI(TAG, "usbflash remains mounted locally for NCM tether profile");
    }
#endif
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_RELEASE_SERIAL, ESP_OK);
    }
    release_usb_serial_jtag_driver();

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
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_TINYUSB_INSTALL, ESP_OK);
    }
    const esp_err_t tinyusb_err = tinyusb_driver_install(&tusb_cfg);
    if (tinyusb_err != ESP_OK) {
        if (tether_activation) {
            tether_diag_mark(FACULTY175_USB_TETHER_STAGE_TINYUSB_INSTALL, tinyusb_err);
        }
        restore_usb_serial_jtag();
#if CONFIG_TINYUSB_MSC_ENABLED
        if (s_storage_ready && s_storage_exposed_to_host) {
            (void)tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
            s_storage_exposed_to_host = false;
        }
#endif
        return tinyusb_err;
    }

#if CONFIG_TINYUSB_NET_MODE_NCM
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_NCM_INIT, ESP_OK);
    }
    const esp_err_t ncm_init_err = faculty175_usb_ncm_init();
    if (ncm_init_err != ESP_OK) {
        if (tether_activation) {
            tether_diag_mark(FACULTY175_USB_TETHER_STAGE_NCM_INIT, ncm_init_err);
            (void)tinyusb_driver_uninstall();
            tether_diag_mark(FACULTY175_USB_TETHER_STAGE_ROLLBACK, ncm_init_err);
            restore_usb_serial_jtag();
        }
        return ncm_init_err;
    }
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_WAIT_HOST, ESP_OK);
    }
    const esp_err_t ncm_host_err = faculty175_usb_ncm_wait_for_host(pdMS_TO_TICKS(FACULTY175_USB_NCM_HOST_TIMEOUT_MS));
    if (ncm_host_err != ESP_OK) {
        if (tether_activation) {
            tether_diag_mark(FACULTY175_USB_TETHER_STAGE_WAIT_HOST, ncm_host_err);
        }
        ESP_LOGW(TAG,
                 "NCM host did not initialize within %u ms: %s; rolling back to Serial/JTAG",
                 FACULTY175_USB_NCM_HOST_TIMEOUT_MS,
                 esp_err_to_name(ncm_host_err));
        const esp_err_t uninstall_err = tinyusb_driver_uninstall();
        if (uninstall_err != ESP_OK) {
            ESP_LOGE(TAG, "TinyUSB teardown failed: %s", esp_err_to_name(uninstall_err));
        }
        if (tether_activation) {
            tether_diag_mark(FACULTY175_USB_TETHER_STAGE_ROLLBACK, ncm_host_err);
        }
        restore_usb_serial_jtag();
#if CONFIG_TINYUSB_MSC_ENABLED
        if (s_storage_ready && s_storage_exposed_to_host) {
            const esp_err_t mount_err = tinyusb_msc_storage_mount(FACULTY175_USBFLASH_BASE_PATH);
            if (mount_err != ESP_OK) {
                ESP_LOGW(TAG, "usbflash local remount failed after NCM rollback: %s", esp_err_to_name(mount_err));
            }
            s_storage_exposed_to_host = false;
        }
#endif
        return ncm_host_err;
    }
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_HTTP_READY, ESP_OK);
    }
#endif

#if CONFIG_TINYUSB_CDC_ENABLED
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
#endif

    s_screen_profile_active = true;
    if (tether_activation) {
        tether_diag_mark(FACULTY175_USB_TETHER_STAGE_READY, ESP_OK);
    }
    ESP_LOGI(TAG,
             "USB profile: TinyUSB CDC+MSC%s%s (usb-screen active); usbflash=%s host=%s",
             faculty175_usb_ncm_ready() ? "+NCM" : "",
             faculty175_km_enabled() ? "+HID keyboard+mouse" : "",
             s_storage_mounted ? "mounted" : "unmounted",
             storage_host_in_use() ? "yes" : "no");
    return ESP_OK;
#endif
}

bool faculty175_usb_screen_profile_active(void)
{
    return s_screen_profile_active;
}

esp_err_t faculty175_usb_tether_start(void)
{
#if !CONFIG_TINYUSB_NET_MODE_NCM
    return ESP_ERR_NOT_SUPPORTED;
#else
    tether_diag_attempt();
    s_tether_mode_active = true;
    if (!s_screen_profile_active) {
        s_tether_activation_pending = true;
    }
    const esp_err_t err = faculty175_usb_set_screen_face_active(true);
    s_tether_activation_pending = false;
    if (err != ESP_OK) {
        s_tether_mode_active = false;
    }
    return err;
#endif
}

bool faculty175_usb_tether_mode_active(void)
{
#if !CONFIG_TINYUSB_NET_MODE_NCM
    return false;
#else
    return s_tether_mode_active;
#endif
}

const char *faculty175_usb_tether_last_stage(void)
{
    tether_diag_init_once();
    return tether_stage_name((faculty175_usb_tether_stage_t)s_tether_diag.last_stage);
}

esp_err_t faculty175_usb_tether_last_error(void)
{
    tether_diag_init_once();
    return (esp_err_t)s_tether_diag.last_error;
}

uint32_t faculty175_usb_tether_attempt_count(void)
{
    tether_diag_init_once();
    return s_tether_diag.attempts;
}

uint32_t faculty175_usb_tether_last_uptime_ms(void)
{
    tether_diag_init_once();
    return s_tether_diag.last_uptime_ms;
}

esp_reset_reason_t faculty175_usb_tether_boot_reset_reason(void)
{
    return esp_reset_reason();
}

bool faculty175_usb_tether_ready(void)
{
#if !CONFIG_TINYUSB_NET_MODE_NCM
    return false;
#else
    return faculty175_usb_ncm_ready();
#endif
}

const esp_ip4_addr_t *faculty175_usb_tether_ip(void)
{
#if !CONFIG_TINYUSB_NET_MODE_NCM
    return NULL;
#else
    return faculty175_usb_ncm_ip();
#endif
}

bool faculty175_usb_auto_tether_enabled(void)
{
    usb_auto_tether_load_once();
    return s_auto_tether_enabled;
}

esp_err_t faculty175_usb_auto_tether_set_enabled(bool enabled)
{
    usb_auto_tether_load_once();
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACULTY175_USB_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, FACULTY175_USB_AUTO_TETHER_KEY, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        s_auto_tether_enabled = enabled;
    }
    return err;
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
