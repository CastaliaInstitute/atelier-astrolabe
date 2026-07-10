#include "faculty175_ota.h"

#include <ctype.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "cJSON.h"

#include "astrolabe_faculty175_face.h"
#include "astrolabe_faculty175_ota_key.h"
#include "faculty175_log.h"
#include "faculty175_storage.h"
#include "faculty175_usb.h"

static const char *TAG = "faculty175_ota";

#define OTA_NVS_NS "ota"
#define OTA_NVS_URL "url"
#define OTA_NVS_PENDING "pending"
#define OTA_NVS_PENDING_SHA "pending_sha256"
#define OTA_NVS_PENDING_SIZE "pending_size"
#define OTA_NVS_LAST_SHA "last_sha256"
#define OTA_NVS_BOOT_PRODUCT "boot_product"
#define OTA_NVS_AUTO_INTERVAL_S "auto_interval_s"
#define OTA_URL_MAX 256
#define OTA_SHA256_HEX_LEN 64
#define OTA_DEVICE_MAC_LEN 17
#define OTA_DEVICE_LIST_MAX 512
#define OTA_SIGNATURE_B64_MAX 128
#define OTA_CANONICAL_MAX 1024
#define OTA_MANIFEST_MAX_BYTES 4096
#define OTA_IO_BUFFER_BYTES 2048
#define OTA_TASK_STACK_BYTES 6144
#define OTA_AUTO_TASK_STACK_BYTES 6144
#define OTA_MIN_INTERNAL_FREE (32 * 1024)
#define OTA_MIN_LARGEST_BLOCK (16 * 1024)
#define OTA_AUTO_MANIFEST_URL "https://astrolabe.castalia.institute/releases/integration/" ASTROLABE_FACULTY_OTA_CHANNEL "/ota-manifest.json"
#define OTA_AUTO_INITIAL_DELAY_MS 20000
#define OTA_AUTO_DEFAULT_INTERVAL_S (15 * 60)
#define OTA_AUTO_MIN_INTERVAL_S 60
#define OTA_LOCAL_PATH_MAX 256
#define OTA_LOCAL_DEFAULT_RELATIVE "update/astrolabe175c.bin"
#define OTA_DEV_SKIP_TLS_SERVER_VERIFY 1

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RUNNING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

static volatile ota_state_t s_ota_state;
static char s_ota_last[160];
static bool s_ota_auto_started;
static volatile bool s_ota_auto_paused;

static void set_last(const char *fmt, ...);

static void *ota_scratch_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

typedef struct {
    char url[OTA_URL_MAX];
    char expected_sha256[OTA_SHA256_HEX_LEN + 1];
    int64_t expected_size;
    bool from_recovery_request;
    bool manifest_url;
    bool local_file;
} ota_job_t;

static const char *part_label(const esp_partition_t *part)
{
    return part != NULL ? part->label : "-";
}

static bool is_factory_partition(const esp_partition_t *part)
{
    return part != NULL && part->type == ESP_PARTITION_TYPE_APP &&
           part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;
}

static bool is_http_url(const char *url)
{
    return url != NULL && (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0);
}

static esp_err_t (*ota_crt_bundle_attach(void))(void *)
{
#if OTA_DEV_SKIP_TLS_SERVER_VERIFY
    return NULL;
#else
    return esp_crt_bundle_attach;
#endif
}

static bool is_absolute_path(const char *path)
{
    return path != NULL && path[0] == '/';
}

static bool is_sha256_hex(const char *hex)
{
    if (hex == NULL || strlen(hex) != OTA_SHA256_HEX_LEN) {
        return false;
    }
    for (size_t i = 0; i < OTA_SHA256_HEX_LEN; ++i) {
        if (!isxdigit((unsigned char)hex[i])) {
            return false;
        }
    }
    return true;
}

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t cap)
{
    static const char alphabet[] = "0123456789abcdef";
    if (out == NULL || cap < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = alphabet[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = alphabet[bytes[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static esp_err_t get_device_mac(char *out, size_t cap)
{
    if (out == NULL || cap < OTA_DEVICE_MAC_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(out,
             cap,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
    return ESP_OK;
}

static bool is_mac_hex(char c)
{
    return isxdigit((unsigned char)c) != 0;
}

static bool normalize_mac(const char *in, char *out, size_t cap)
{
    if (in == NULL || out == NULL || cap < OTA_DEVICE_MAC_LEN + 1) {
        return false;
    }
    char hex[12];
    size_t n = 0;
    for (const char *p = in; *p != '\0'; ++p) {
        if (*p == ':' || *p == '-' || *p == ' ') {
            continue;
        }
        if (!is_mac_hex(*p) || n >= sizeof(hex)) {
            return false;
        }
        hex[n++] = (char)tolower((unsigned char)*p);
    }
    if (n != sizeof(hex)) {
        return false;
    }
    snprintf(out,
             cap,
             "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex[0],
             hex[1],
             hex[2],
             hex[3],
             hex[4],
             hex[5],
             hex[6],
             hex[7],
             hex[8],
             hex[9],
             hex[10],
             hex[11]);
    return true;
}

static esp_err_t manifest_devices_csv(const cJSON *devices, char *out, size_t cap, bool *authorized)
{
    if (out == NULL || cap == 0 || authorized == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    *authorized = false;
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) <= 0) {
        set_last("manifest missing devices");
        return ESP_ERR_INVALID_ARG;
    }

    char own_mac[OTA_DEVICE_MAC_LEN + 1];
    esp_err_t err = get_device_mac(own_mac, sizeof(own_mac));
    if (err != ESP_OK) {
        set_last("device mac unavailable");
        return err;
    }

    size_t used = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, devices) {
        if (!cJSON_IsString(item)) {
            set_last("manifest invalid device");
            return ESP_ERR_INVALID_ARG;
        }
        char mac[OTA_DEVICE_MAC_LEN + 1];
        if (!normalize_mac(item->valuestring, mac, sizeof(mac))) {
            set_last("manifest invalid device mac");
            return ESP_ERR_INVALID_ARG;
        }
        if (strcmp(mac, own_mac) == 0) {
            *authorized = true;
        }
        const size_t need = strlen(mac) + (used > 0 ? 1 : 0);
        if (used + need + 1 > cap) {
            set_last("manifest devices too large");
            return ESP_ERR_INVALID_SIZE;
        }
        if (used > 0) {
            out[used++] = ',';
        }
        memcpy(out + used, mac, strlen(mac));
        used += strlen(mac);
        out[used] = '\0';
    }
    if (!*authorized) {
        set_last("manifest device not provisioned");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t manifest_canonical(char *out,
                                    size_t cap,
                                    const char *channel,
                                    const char *firmware_url,
                                    const char *sha256,
                                    int64_t bytes,
                                    const char *devices_csv)
{
    if (out == NULL || channel == NULL || firmware_url == NULL || sha256 == NULL || devices_csv == NULL ||
        devices_csv[0] == '\0' || bytes <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const int n = snprintf(out,
                           cap,
                           "ota_channel=%s\nfirmware_url=%s\nsha256=%s\nbytes=%lld\ndevices=%s\n",
                           channel,
                           firmware_url,
                           sha256,
                           bytes,
                           devices_csv);
    if (n <= 0 || (size_t)n >= cap) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t verify_manifest_signature(const char *canonical, const char *signature_b64)
{
    if (canonical == NULL || signature_b64 == NULL || signature_b64[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t signature[96];
    size_t signature_len = 0;
    int rc = mbedtls_base64_decode(signature,
                                   sizeof(signature),
                                   &signature_len,
                                   (const unsigned char *)signature_b64,
                                   strlen(signature_b64));
    if (rc != 0 || signature_len == 0) {
        set_last("manifest signature base64 invalid");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t digest[32];
    rc = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    (const unsigned char *)canonical,
                    strlen(canonical),
                    digest);
    if (rc != 0) {
        set_last("manifest signature digest failed");
        return ESP_FAIL;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    rc = mbedtls_pk_parse_public_key(&pk,
                                     (const unsigned char *)ASTROLABE_FACULTY175_OTA_PUBKEY_PEM,
                                     strlen(ASTROLABE_FACULTY175_OTA_PUBKEY_PEM) + 1);
    if (rc != 0) {
        mbedtls_pk_free(&pk);
        set_last("manifest public key parse failed");
        return ESP_FAIL;
    }
    if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA)) {
        mbedtls_pk_free(&pk);
        set_last("manifest public key not ECDSA");
        return ESP_FAIL;
    }
    rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, signature_len);
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        set_last("manifest signature invalid");
        return ESP_ERR_INVALID_CRC;
    }
    FACULTY175_LOG_STAGE(TAG, "ota", "manifest signature ok");
    return ESP_OK;
}

static void set_last(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_ota_last, sizeof(s_ota_last), fmt, ap);
    va_end(ap);
}

static bool ota_heap_ready(void)
{
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (free_internal < OTA_MIN_INTERNAL_FREE || largest_internal < OTA_MIN_LARGEST_BLOCK) {
        set_last("low heap free=%u largest=%u", (unsigned)free_internal, (unsigned)largest_internal);
        FACULTY175_LOG_STAGE_W(TAG, "ota", "%s", s_ota_last);
        return false;
    }
    return true;
}

static esp_err_t nvs_set_pending_job(const char *url, const char *sha256, int64_t size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, OTA_NVS_URL, url);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, OTA_NVS_PENDING, 1);
    }
    if (err == ESP_OK) {
        if (sha256 != NULL && sha256[0] != '\0') {
            err = nvs_set_str(nvs, OTA_NVS_PENDING_SHA, sha256);
        } else {
            (void)nvs_erase_key(nvs, OTA_NVS_PENDING_SHA);
        }
    }
    if (err == ESP_OK) {
        if (size > 0) {
            err = nvs_set_i64(nvs, OTA_NVS_PENDING_SIZE, size);
        } else {
            (void)nvs_erase_key(nvs, OTA_NVS_PENDING_SIZE);
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t nvs_set_pending_url(const char *url)
{
    return nvs_set_pending_job(url, NULL, 0);
}

static esp_err_t nvs_get_pending_url(char *url, size_t cap, bool *pending)
{
    if (pending != NULL) {
        *pending = false;
    }
    if (url != NULL && cap > 0) {
        url[0] = '\0';
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t p = 0;
    err = nvs_get_u8(nvs, OTA_NVS_PENDING, &p);
    if (err == ESP_OK && p != 0 && url != NULL && cap > 0) {
        size_t len = cap;
        err = nvs_get_str(nvs, OTA_NVS_URL, url, &len);
    }
    nvs_close(nvs);
    if (err == ESP_OK && pending != NULL) {
        *pending = p != 0 && url != NULL && url[0] != '\0';
    }
    return err;
}

static esp_err_t nvs_get_pending_job(char *url, size_t url_cap, char *sha256, size_t sha_cap,
                                     int64_t *size, bool *pending)
{
    if (sha256 != NULL && sha_cap > 0) {
        sha256[0] = '\0';
    }
    if (size != NULL) {
        *size = 0;
    }
    esp_err_t err = nvs_get_pending_url(url, url_cap, pending);
    if (err != ESP_OK || pending == NULL || !*pending) {
        return err;
    }
    nvs_handle_t nvs;
    err = nvs_open(OTA_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    if (sha256 != NULL && sha_cap > 0) {
        size_t len = sha_cap;
        const esp_err_t sha_err = nvs_get_str(nvs, OTA_NVS_PENDING_SHA, sha256, &len);
        if (sha_err != ESP_OK) {
            sha256[0] = '\0';
        }
    }
    if (size != NULL) {
        int64_t stored_size = 0;
        if (nvs_get_i64(nvs, OTA_NVS_PENDING_SIZE, &stored_size) == ESP_OK && stored_size > 0) {
            *size = stored_size;
        }
    }
    nvs_close(nvs);
    return ESP_OK;
}

static void nvs_clear_pending_url(void)
{
    nvs_handle_t nvs;
    if (nvs_open(OTA_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    (void)nvs_erase_key(nvs, OTA_NVS_URL);
    (void)nvs_erase_key(nvs, OTA_NVS_PENDING);
    (void)nvs_erase_key(nvs, OTA_NVS_PENDING_SHA);
    (void)nvs_erase_key(nvs, OTA_NVS_PENDING_SIZE);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static esp_err_t nvs_get_last_sha(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = cap;
    err = nvs_get_str(nvs, OTA_NVS_LAST_SHA, out, &len);
    nvs_close(nvs);
    return err;
}

static esp_err_t nvs_set_last_sha(const char *sha)
{
    if (sha == NULL || strlen(sha) != OTA_SHA256_HEX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, OTA_NVS_LAST_SHA, sha);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t nvs_get_boot_product(bool *enabled)
{
    if (enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *enabled = false;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t value = 0;
    err = nvs_get_u8(nvs, OTA_NVS_BOOT_PRODUCT, &value);
    nvs_close(nvs);
    if (err == ESP_OK) {
        *enabled = value != 0;
    }
    return err;
}

static esp_err_t nvs_set_boot_product(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, OTA_NVS_BOOT_PRODUCT, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static uint32_t nvs_get_auto_interval_s(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return OTA_AUTO_DEFAULT_INTERVAL_S;
    }
    uint32_t interval_s = OTA_AUTO_DEFAULT_INTERVAL_S;
    err = nvs_get_u32(nvs, OTA_NVS_AUTO_INTERVAL_S, &interval_s);
    nvs_close(nvs);
    if (err != ESP_OK) {
        return OTA_AUTO_DEFAULT_INTERVAL_S;
    }
    if (interval_s > 0 && interval_s < OTA_AUTO_MIN_INTERVAL_S) {
        return OTA_AUTO_MIN_INTERVAL_S;
    }
    return interval_s;
}

static esp_err_t nvs_set_auto_interval_s(uint32_t interval_s)
{
    if (interval_s > 0 && interval_s < OTA_AUTO_MIN_INTERVAL_S) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(nvs, OTA_NVS_AUTO_INTERVAL_S, interval_s);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t reboot_to_factory(void)
{
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    return esp_ota_set_boot_partition(factory);
}

static bool is_valid_app_image(const esp_partition_t *part)
{
    if (part == NULL) {
        return false;
    }
    const esp_partition_pos_t pos = {
        .offset = part->address,
        .size = part->size,
    };
    esp_image_metadata_t metadata = {};
    return esp_image_verify(ESP_IMAGE_VERIFY, &pos, &metadata) == ESP_OK;
}

static const esp_partition_t *find_valid_product_partition(void)
{
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    if (configured != NULL && configured->type == ESP_PARTITION_TYPE_APP &&
        (configured->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
         configured->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) &&
        is_valid_app_image(configured)) {
        return configured;
    }

    const esp_partition_t *slots[] = {
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL),
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL),
    };
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        if (is_valid_app_image(slots[i])) {
            return slots[i];
        }
    }
    return NULL;
}

static void print_status(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    char pending_url[OTA_URL_MAX];
    bool pending = false;
    bool boot_product = false;
    (void)nvs_get_pending_url(pending_url, sizeof(pending_url), &pending);
    (void)nvs_get_boot_product(&boot_product);
    const uint32_t auto_interval_s = nvs_get_auto_interval_s();
    const esp_app_desc_t *desc = esp_app_get_description();
    printf("ota: status state=%d running=%s boot=%s next=%s version=%s pending=%s boot_product=%s auto_interval_s=%u\n",
           (int)s_ota_state,
           part_label(running),
           part_label(boot),
           part_label(next),
           desc != NULL ? desc->version : "-",
           pending ? "yes" : "no",
           boot_product ? "yes" : "no",
           (unsigned)auto_interval_s);
    if (pending) {
        printf("ota: pending url=%s\n", pending_url);
    }
    if (s_ota_last[0] != '\0') {
        printf("ota: last %s\n", s_ota_last);
    }
    fflush(stdout);
}

static esp_err_t fetch_manifest(const char *manifest_url, ota_job_t *job)
{
    if (!is_http_url(manifest_url) || job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_heap_ready()) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t cfg = {
        .url = manifest_url,
        .timeout_ms = 12000,
        .buffer_size = 1024,
        .crt_bundle_attach = ota_crt_bundle_attach(),
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        set_last("manifest http init failed");
        return ESP_ERR_NO_MEM;
    }

    char *body = ota_scratch_malloc(OTA_MANIFEST_MAX_BYTES + 1);
    esp_err_t err = body != NULL ? ESP_OK : ESP_ERR_NO_MEM;
    int64_t content_len = -1;
    int status = 0;
    if (err == ESP_OK) {
        esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-OTA/1");
        err = esp_http_client_open(client, 0);
    }
    if (err == ESP_OK) {
        content_len = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            set_last("manifest http status=%d", status);
            err = ESP_FAIL;
        } else if (content_len <= 0 || content_len > OTA_MANIFEST_MAX_BYTES) {
            set_last("manifest size=%lld", content_len);
            err = ESP_ERR_INVALID_SIZE;
        }
    }

    size_t total = 0;
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, body + total, OTA_MANIFEST_MAX_BYTES - total);
        if (n < 0) {
            set_last("manifest read failed");
            err = ESP_FAIL;
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
        if (total > OTA_MANIFEST_MAX_BYTES) {
            set_last("manifest too large");
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (err == ESP_OK && content_len > 0 && total != (size_t)content_len) {
        set_last("manifest short got=%u expected=%lld", (unsigned)total, content_len);
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) {
        body[total] = '\0';
        cJSON *root = cJSON_ParseWithLength(body, total);
        if (root == NULL) {
            set_last("manifest json parse failed");
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            const cJSON *channel = cJSON_GetObjectItemCaseSensitive(root, "ota_channel");
            const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "firmware_url");
            const cJSON *sha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
            const cJSON *bytes = cJSON_GetObjectItemCaseSensitive(root, "bytes");
            const cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
            const cJSON *sig_alg = cJSON_GetObjectItemCaseSensitive(root, "sig_alg");
            const cJSON *signature = cJSON_GetObjectItemCaseSensitive(root, "signature");
            if (!cJSON_IsString(channel) || strcmp(channel->valuestring, ASTROLABE_FACULTY_OTA_CHANNEL) != 0) {
                set_last("manifest wrong channel");
                err = ESP_ERR_INVALID_VERSION;
            } else if (!cJSON_IsString(url) || !is_http_url(url->valuestring)) {
                set_last("manifest missing firmware_url");
                err = ESP_ERR_INVALID_ARG;
            } else if (!cJSON_IsString(sha) || !is_sha256_hex(sha->valuestring)) {
                set_last("manifest missing sha256");
                err = ESP_ERR_INVALID_ARG;
            } else if (!cJSON_IsNumber(bytes) || bytes->valuedouble <= 0) {
                set_last("manifest missing bytes");
                err = ESP_ERR_INVALID_SIZE;
            } else if (!cJSON_IsString(sig_alg) ||
                       strcmp(sig_alg->valuestring, ASTROLABE_FACULTY175_OTA_SIG_ALG) != 0) {
                set_last("manifest missing sig_alg");
                err = ESP_ERR_INVALID_VERSION;
            } else if (!cJSON_IsString(signature) || strlen(signature->valuestring) > OTA_SIGNATURE_B64_MAX) {
                set_last("manifest missing signature");
                err = ESP_ERR_INVALID_ARG;
            } else {
                const int64_t expected_size = (int64_t)bytes->valuedouble;
                char canonical[OTA_CANONICAL_MAX];
                char devices_csv[OTA_DEVICE_LIST_MAX];
                bool authorized = false;
                err = manifest_devices_csv(devices, devices_csv, sizeof(devices_csv), &authorized);
                if (err == ESP_OK) {
                    err = manifest_canonical(canonical,
                                             sizeof(canonical),
                                             channel->valuestring,
                                             url->valuestring,
                                             sha->valuestring,
                                             expected_size,
                                             devices_csv);
                }
                if (err == ESP_OK) {
                    err = verify_manifest_signature(canonical, signature->valuestring);
                }
                if (err == ESP_OK) {
                    strlcpy(job->url, url->valuestring, sizeof(job->url));
                    strlcpy(job->expected_sha256, sha->valuestring, sizeof(job->expected_sha256));
                    job->expected_size = expected_size;
                }
            }
            cJSON_Delete(root);
        }
    }
    free(body);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t stream_install_url(const char *url,
                                    const char *expected_sha256,
                                    int64_t expected_size,
                                    const esp_partition_t **out_target)
{
    if (!is_http_url(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_heap_ready()) {
        return ESP_ERR_NO_MEM;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        set_last("no inactive OTA partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (running != NULL && target->address == running->address) {
        set_last("refusing to write running partition %s", part_label(target));
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = OTA_IO_BUFFER_BYTES,
        .crt_bundle_attach = ota_crt_bundle_attach(),
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        set_last("http init failed");
        return ESP_ERR_NO_MEM;
    }

    esp_ota_handle_t ota = 0;
    uint8_t *buf = ota_scratch_malloc(OTA_IO_BUFFER_BYTES);
    esp_err_t err = buf != NULL ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-OTA/1");
        err = esp_http_client_open(client, 0);
    }
    int status = 0;
    int64_t content_len = -1;
    if (err == ESP_OK) {
        content_len = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            set_last("http status=%d", status);
            err = ESP_FAIL;
        } else if (content_len <= 0) {
            set_last("missing content length");
            err = ESP_FAIL;
        } else if (expected_size > 0 && content_len != expected_size) {
            set_last("manifest size mismatch got=%lld expected=%lld", content_len, expected_size);
            err = ESP_ERR_INVALID_SIZE;
        } else if ((uint64_t)content_len > target->size) {
            set_last("image too large len=%lld slot=%u", content_len, (unsigned)target->size);
            err = ESP_ERR_INVALID_SIZE;
        }
    }
    if (err == ESP_OK) {
        FACULTY175_LOG_STAGE(TAG, "ota", "install %lldB -> %s @0x%lx", content_len, target->label,
                       (unsigned long)target->address);
        err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    }

    size_t total = 0;
    bool first_chunk = true;
    mbedtls_sha256_context sha_ctx;
    bool sha_started = false;
    if (err == ESP_OK && expected_sha256 != NULL && expected_sha256[0] != '\0') {
        if (!is_sha256_hex(expected_sha256)) {
            set_last("invalid expected sha256");
            err = ESP_ERR_INVALID_ARG;
        } else {
            mbedtls_sha256_init(&sha_ctx);
            if (mbedtls_sha256_starts(&sha_ctx, 0) != 0) {
                set_last("sha256 start failed");
                err = ESP_FAIL;
            } else {
                sha_started = true;
            }
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, OTA_IO_BUFFER_BYTES);
        if (n < 0) {
            set_last("http read failed");
            err = ESP_FAIL;
            break;
        }
        if (n == 0) {
            break;
        }
        if (first_chunk) {
            first_chunk = false;
            if (n < (int)sizeof(esp_image_header_t)) {
                set_last("first chunk too small");
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            const esp_image_header_t *hdr = (const esp_image_header_t *)buf;
            if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
                set_last("bad image magic=0x%02x", hdr->magic);
                err = ESP_ERR_INVALID_VERSION;
                break;
            }
        }
        err = esp_ota_write(ota, buf, (size_t)n);
        if (err != ESP_OK) {
            set_last("ota write failed: %s", esp_err_to_name(err));
            break;
        }
        if (sha_started && mbedtls_sha256_update(&sha_ctx, buf, (size_t)n) != 0) {
            set_last("sha256 update failed");
            err = ESP_FAIL;
            break;
        }
        total += (size_t)n;
        if ((total % (128 * 1024)) < (size_t)n || total == (size_t)n) {
            FACULTY175_LOG_STAGE(TAG, "ota", "wrote %u/%lld", (unsigned)total, content_len);
        }
        if (content_len > 0 && total > (size_t)content_len) {
            set_last("body longer than content length");
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (err == ESP_OK && content_len > 0 && total != (size_t)content_len) {
        set_last("short image got=%u expected=%lld", (unsigned)total, content_len);
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && sha_started) {
        uint8_t digest[32];
        char actual[OTA_SHA256_HEX_LEN + 1];
        if (mbedtls_sha256_finish(&sha_ctx, digest) != 0) {
            set_last("sha256 finish failed");
            err = ESP_FAIL;
        } else {
            bytes_to_hex(digest, sizeof(digest), actual, sizeof(actual));
            if (strcasecmp(actual, expected_sha256) != 0) {
                set_last("sha256 mismatch");
                err = ESP_ERR_INVALID_CRC;
            } else {
                FACULTY175_LOG_STAGE(TAG, "ota", "sha256 ok %s", actual);
            }
        }
        mbedtls_sha256_free(&sha_ctx);
        sha_started = false;
    }
    if (err == ESP_OK) {
        err = esp_ota_end(ota);
        ota = 0;
        if (err != ESP_OK) {
            set_last("ota end failed: %s", esp_err_to_name(err));
        }
    }
    if (err == ESP_OK) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(target, &desc) == ESP_OK) {
            FACULTY175_LOG_STAGE(TAG, "ota", "image version=%s project=%s", desc.version, desc.project_name);
        }
        err = esp_ota_set_boot_partition(target);
        if (err == ESP_OK) {
            set_last("installed %uB -> %s", (unsigned)total, target->label);
            if (out_target != NULL) {
                *out_target = target;
            }
        } else {
            set_last("set boot failed: %s", esp_err_to_name(err));
        }
    }
    if (ota != 0) {
        (void)esp_ota_abort(ota);
    }
    free(buf);
    if (sha_started) {
        mbedtls_sha256_free(&sha_ctx);
    }
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t resolve_local_ota_path(const char *input, char *out, size_t cap)
{
    if (out == NULL || cap == 0 || input == NULL || input[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (is_absolute_path(input)) {
        struct stat st = {};
        if (stat(input, &st) == 0 && S_ISREG(st.st_mode)) {
            strlcpy(out, input, cap);
            return ESP_OK;
        }
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = faculty175_usb_storage_claim();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (faculty175_storage_resolve_path(input, out, cap)) {
        return ESP_OK;
    }
    return faculty175_usb_storage_resolve_path(input, out, cap) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t stream_install_file(const char *path,
                                     const char *expected_sha256,
                                     int64_t expected_size,
                                     const esp_partition_t **out_target)
{
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_heap_ready()) {
        return ESP_ERR_NO_MEM;
    }

    struct stat st = {};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        set_last("file not found");
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        set_last("no inactive OTA partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (running != NULL && target->address == running->address) {
        set_last("refusing to write running partition %s", part_label(target));
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t content_len = (int64_t)st.st_size;
    if (content_len <= 0) {
        set_last("empty file");
        return ESP_ERR_INVALID_SIZE;
    }
    if (expected_size > 0 && content_len != expected_size) {
        set_last("file size mismatch got=%lld expected=%lld", content_len, expected_size);
        return ESP_ERR_INVALID_SIZE;
    }
    if ((uint64_t)content_len > target->size) {
        set_last("image too large len=%lld slot=%u", content_len, (unsigned)target->size);
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        set_last("open failed");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota = 0;
    uint8_t *buf = ota_scratch_malloc(OTA_IO_BUFFER_BYTES);
    esp_err_t err = buf != NULL ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        FACULTY175_LOG_STAGE(TAG, "ota", "install file %s %lldB -> %s @0x%lx", path, content_len, target->label,
                             (unsigned long)target->address);
        err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    }

    size_t total = 0;
    bool first_chunk = true;
    mbedtls_sha256_context sha_ctx;
    bool sha_started = false;
    if (err == ESP_OK && expected_sha256 != NULL && expected_sha256[0] != '\0') {
        if (!is_sha256_hex(expected_sha256)) {
            set_last("invalid expected sha256");
            err = ESP_ERR_INVALID_ARG;
        } else {
            mbedtls_sha256_init(&sha_ctx);
            if (mbedtls_sha256_starts(&sha_ctx, 0) != 0) {
                set_last("sha256 start failed");
                err = ESP_FAIL;
            } else {
                sha_started = true;
            }
        }
    }
    while (err == ESP_OK) {
        const size_t n = fread(buf, 1, OTA_IO_BUFFER_BYTES, fp);
        if (n == 0) {
            if (ferror(fp)) {
                set_last("file read failed");
                err = ESP_FAIL;
            }
            break;
        }
        if (first_chunk) {
            first_chunk = false;
            if (n < sizeof(esp_image_header_t)) {
                set_last("first chunk too small");
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            const esp_image_header_t *hdr = (const esp_image_header_t *)buf;
            if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
                set_last("bad image magic=0x%02x", hdr->magic);
                err = ESP_ERR_INVALID_VERSION;
                break;
            }
        }
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            set_last("ota write failed: %s", esp_err_to_name(err));
            break;
        }
        if (sha_started && mbedtls_sha256_update(&sha_ctx, buf, n) != 0) {
            set_last("sha256 update failed");
            err = ESP_FAIL;
            break;
        }
        total += n;
        if ((total % (128 * 1024)) < n || total == n) {
            FACULTY175_LOG_STAGE(TAG, "ota", "wrote %u/%lld", (unsigned)total, content_len);
        }
        if (content_len > 0 && total > (size_t)content_len) {
            set_last("body longer than file size");
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
    }
    if (err == ESP_OK && total != (size_t)content_len) {
        set_last("short image got=%u expected=%lld", (unsigned)total, content_len);
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && sha_started) {
        uint8_t digest[32];
        char actual[OTA_SHA256_HEX_LEN + 1];
        if (mbedtls_sha256_finish(&sha_ctx, digest) != 0) {
            set_last("sha256 finish failed");
            err = ESP_FAIL;
        } else {
            bytes_to_hex(digest, sizeof(digest), actual, sizeof(actual));
            if (strcasecmp(actual, expected_sha256) != 0) {
                set_last("sha256 mismatch");
                err = ESP_ERR_INVALID_CRC;
            } else {
                FACULTY175_LOG_STAGE(TAG, "ota", "sha256 ok %s", actual);
            }
        }
        mbedtls_sha256_free(&sha_ctx);
        sha_started = false;
    }
    if (err == ESP_OK) {
        err = esp_ota_end(ota);
        ota = 0;
        if (err != ESP_OK) {
            set_last("ota end failed: %s", esp_err_to_name(err));
        }
    }
    if (err == ESP_OK) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(target, &desc) == ESP_OK) {
            FACULTY175_LOG_STAGE(TAG, "ota", "image version=%s project=%s", desc.version, desc.project_name);
        }
        err = esp_ota_set_boot_partition(target);
        if (err == ESP_OK) {
            set_last("installed %uB -> %s", (unsigned)total, target->label);
            if (out_target != NULL) {
                *out_target = target;
            }
        } else {
            set_last("set boot failed: %s", esp_err_to_name(err));
        }
    }
    if (ota != 0) {
        (void)esp_ota_abort(ota);
    }
    free(buf);
    if (sha_started) {
        mbedtls_sha256_free(&sha_ctx);
    }
    fclose(fp);
    return err;
}

static void ota_task(void *arg)
{
    ota_job_t *job = (ota_job_t *)arg;
    s_ota_state = OTA_STATE_RUNNING;
    const esp_partition_t *target = NULL;
    ota_job_t install = *job;
    esp_err_t err = ESP_OK;
    if (job->manifest_url) {
        FACULTY175_LOG_STAGE(TAG, "ota", "manifest %s", job->url);
        memset(&install, 0, sizeof(install));
        install.from_recovery_request = job->from_recovery_request;
        err = fetch_manifest(job->url, &install);
    }
    if (err == ESP_OK && job->manifest_url && install.expected_sha256[0] != '\0') {
        char last_sha[OTA_SHA256_HEX_LEN + 1];
        if (nvs_get_last_sha(last_sha, sizeof(last_sha)) == ESP_OK &&
            strcmp(last_sha, install.expected_sha256) == 0) {
            s_ota_state = OTA_STATE_IDLE;
            set_last("manifest already installed %.12s", install.expected_sha256);
            FACULTY175_LOG_STAGE(TAG, "ota", "%s", s_ota_last);
            free(job);
            vTaskDelete(NULL);
            return;
        }
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (err == ESP_OK && job->manifest_url && !is_factory_partition(running)) {
        err = nvs_set_pending_job(install.url, install.expected_sha256, install.expected_size);
        if (err == ESP_OK && install.expected_sha256[0] != '\0') {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_last_sha(install.expected_sha256));
        }
        if (err == ESP_OK) {
            err = reboot_to_factory();
        }
        if (err == ESP_OK) {
            s_ota_state = OTA_STATE_DONE;
            set_last("scheduled factory recovery %.12s", install.expected_sha256);
            FACULTY175_LOG_STAGE(TAG, "ota", "%s", s_ota_last);
            free(job);
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
            vTaskDelete(NULL);
            return;
        }
    }
    if (err == ESP_OK) {
        FACULTY175_LOG_STAGE(TAG, "ota", "%s %s", install.local_file ? "file" : "fetch", install.url);
        err = install.local_file ? stream_install_file(install.url, install.expected_sha256, install.expected_size, &target)
                                 : stream_install_url(install.url, install.expected_sha256, install.expected_size, &target);
    }
    if (err == ESP_OK) {
        s_ota_state = OTA_STATE_DONE;
        if (install.expected_sha256[0] != '\0') {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_last_sha(install.expected_sha256));
        }
        nvs_clear_pending_url();
        FACULTY175_LOG_STAGE(TAG, "ota", "installed boot=%s; rebooting", part_label(target));
        vTaskDelay(pdMS_TO_TICKS(750));
        esp_restart();
    } else {
        s_ota_state = OTA_STATE_ERROR;
        FACULTY175_LOG_STAGE_E(TAG, "ota", "install failed: %s (%s)", esp_err_to_name(err), s_ota_last);
        if (job->from_recovery_request) {
            nvs_clear_pending_url();
        }
    }
    free(job);
    vTaskDelete(NULL);
}

static esp_err_t start_install_job(const ota_job_t *src)
{
    if (s_ota_state == OTA_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (src == NULL || src->url[0] == '\0' || (!src->local_file && !is_http_url(src->url))) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_job_t *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *job = *src;
    s_ota_state = OTA_STATE_RUNNING;
    if (xTaskCreate(ota_task, "ota", OTA_TASK_STACK_BYTES, job, 6, NULL) != pdPASS) {
        s_ota_state = OTA_STATE_ERROR;
        free(job);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t start_install_task(const char *url, const char *expected_sha256, int64_t expected_size,
                                    bool from_recovery_request)
{
    ota_job_t job = {
        .expected_size = expected_size,
        .from_recovery_request = from_recovery_request,
    };
    strlcpy(job.url, url, sizeof(job.url));
    if (expected_sha256 != NULL) {
        strlcpy(job.expected_sha256, expected_sha256, sizeof(job.expected_sha256));
    }
    return start_install_job(&job);
}

static void ota_auto_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(OTA_AUTO_INITIAL_DELAY_MS));
    while (true) {
        const uint32_t interval_s = nvs_get_auto_interval_s();
        if (interval_s > 0 && !s_ota_auto_paused && s_ota_state != OTA_STATE_RUNNING && ota_heap_ready()) {
            ota_job_t job = {
                .manifest_url = true,
            };
            strlcpy(job.url, OTA_AUTO_MANIFEST_URL, sizeof(job.url));
            const esp_err_t err = start_install_job(&job);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                set_last("auto start failed: %s", esp_err_to_name(err));
                FACULTY175_LOG_STAGE_W(TAG, "ota", "%s", s_ota_last);
            }
        }
        vTaskDelay(pdMS_TO_TICKS((interval_s > 0 ? interval_s : OTA_AUTO_DEFAULT_INTERVAL_S) * 1000u));
    }
}

static esp_err_t start_manifest_task(const char *manifest_url)
{
    ota_job_t job = {
        .manifest_url = true,
    };
    strlcpy(job.url, manifest_url, sizeof(job.url));
    return start_install_job(&job);
}

static esp_err_t start_file_install_task(const char *path, const char *expected_sha256)
{
    ota_job_t job = {
        .local_file = true,
    };
    strlcpy(job.url, path, sizeof(job.url));
    if (expected_sha256 != NULL) {
        strlcpy(job.expected_sha256, expected_sha256, sizeof(job.expected_sha256));
    }
    return start_install_job(&job);
}

static const char *skip_spaces(const char *s)
{
    while (s != NULL && *s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static bool take_token(const char **cursor, char *out, size_t cap)
{
    if (cursor == NULL || out == NULL || cap == 0) {
        return false;
    }
    const char *s = skip_spaces(*cursor);
    size_t len = 0;
    while (s[len] != '\0' && !isspace((unsigned char)s[len])) {
        ++len;
    }
    if (len == 0 || len >= cap) {
        out[0] = '\0';
        *cursor = s + len;
        return false;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    *cursor = s + len;
    return true;
}

void faculty175_ota_init(void)
{
    s_ota_state = OTA_STATE_IDLE;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    set_last("boot running=%s boot=%s", part_label(running), part_label(boot));
}

void faculty175_ota_maybe_boot_product(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!is_factory_partition(running)) {
        return;
    }
    bool boot_product = false;
    if (nvs_get_boot_product(&boot_product) != ESP_OK || !boot_product) {
        return;
    }
    const esp_partition_t *product = find_valid_product_partition();
    if (product == NULL) {
        set_last("boot_product set but no valid OTA slot");
        FACULTY175_LOG_STAGE_W(TAG, "ota", "%s", s_ota_last);
        return;
    }
    esp_err_t err = esp_ota_set_boot_partition(product);
    if (err != ESP_OK) {
        set_last("boot_product set boot failed: %s", esp_err_to_name(err));
        FACULTY175_LOG_STAGE_E(TAG, "ota", "%s", s_ota_last);
        return;
    }
    FACULTY175_LOG_STAGE(TAG, "ota", "boot_product -> %s; rebooting", part_label(product));
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

bool faculty175_ota_active(void)
{
    return s_ota_state == OTA_STATE_RUNNING;
}

void faculty175_ota_set_auto_paused(bool paused)
{
    s_ota_auto_paused = paused;
}

void faculty175_ota_maybe_start_recovery_request(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!is_factory_partition(running)) {
        return;
    }
    char url[OTA_URL_MAX];
    char sha[OTA_SHA256_HEX_LEN + 1];
    int64_t size = 0;
    bool pending = false;
    if (nvs_get_pending_job(url, sizeof(url), sha, sizeof(sha), &size, &pending) != ESP_OK || !pending) {
        return;
    }
    FACULTY175_LOG_STAGE(TAG, "ota", "factory recovery request pending");
    esp_err_t err = start_install_task(url, sha[0] != '\0' ? sha : NULL, size, true);
    if (err != ESP_OK) {
        set_last("start failed: %s", esp_err_to_name(err));
        s_ota_state = OTA_STATE_ERROR;
        FACULTY175_LOG_STAGE_E(TAG, "ota", "%s", s_ota_last);
    }
}

void faculty175_ota_start_auto_update_task(void)
{
    if (s_ota_auto_started) {
        return;
    }
    s_ota_auto_started = true;
    if (xTaskCreate(ota_auto_task, "ota_auto", OTA_AUTO_TASK_STACK_BYTES, NULL, 3, NULL) != pdPASS) {
        s_ota_auto_started = false;
        set_last("auto task failed");
        FACULTY175_LOG_STAGE_W(TAG, "ota", "%s", s_ota_last);
    } else {
        FACULTY175_LOG_STAGE(TAG, "ota", "auto manifest %s", OTA_AUTO_MANIFEST_URL);
    }
}

bool faculty175_ota_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "ota") != 0 && strncasecmp(line, "ota ", 4) != 0)) {
        return false;
    }

    const char *sub = skip_spaces(line + 3);
    if (*sub == '\0' || strcasecmp(sub, "help") == 0) {
        printf("ota commands:\n");
        printf("  ota status\n");
        printf("  ota fetch <https-url> [sha256]\n");
        printf("  ota file <usbflash-relative|absolute-path> [sha256]\n");
        printf("  ota usb [sha256]   # default: " OTA_LOCAL_DEFAULT_RELATIVE "\n");
        printf("  ota manifest <https-manifest-url>\n");
        printf("  ota recovery <https-url>\n");
        printf("  ota auto <seconds|off|status>   # default: %u\n", (unsigned)OTA_AUTO_DEFAULT_INTERVAL_S);
        printf("  ota boot ota|factory|status\n");
        printf("  ota factory\n");
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "status") == 0) {
        print_status();
        return true;
    }
    if (strcasecmp(sub, "factory") == 0) {
        esp_err_t err = reboot_to_factory();
        printf("ota: factory %s\n", esp_err_to_name(err));
        fflush(stdout);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
        return true;
    }
    if (strncasecmp(sub, "boot ", 5) == 0) {
        const char *mode = skip_spaces(sub + 5);
        if (strcasecmp(mode, "status") == 0) {
            bool boot_product = false;
            (void)nvs_get_boot_product(&boot_product);
            printf("ota: boot product=%s\n", boot_product ? "yes" : "no");
            fflush(stdout);
            return true;
        }
        if (strcasecmp(mode, "ota") == 0 || strcasecmp(mode, "product") == 0) {
            esp_err_t err = nvs_set_boot_product(true);
            if (err == ESP_OK) {
                err = reboot_to_factory();
            }
            printf("ota: boot ota %s\n", esp_err_to_name(err));
            fflush(stdout);
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(250));
                esp_restart();
            }
            return true;
        }
        if (strcasecmp(mode, "factory") == 0 || strcasecmp(mode, "recovery") == 0) {
            esp_err_t err = nvs_set_boot_product(false);
            if (err == ESP_OK) {
                err = reboot_to_factory();
            }
            printf("ota: boot factory %s\n", esp_err_to_name(err));
            fflush(stdout);
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(250));
                esp_restart();
            }
            return true;
        }
        printf("ota: boot expects ota|factory|status\n");
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "auto") == 0 || strncasecmp(sub, "auto ", 5) == 0) {
        const char *value = skip_spaces(sub + 4);
        if (*value == '\0' || strcasecmp(value, "status") == 0) {
            printf("ota: auto interval_s=%u\n", (unsigned)nvs_get_auto_interval_s());
            fflush(stdout);
            return true;
        }
        uint32_t interval_s = 0;
        esp_err_t err = ESP_OK;
        if (strcasecmp(value, "off") == 0 || strcasecmp(value, "disable") == 0 || strcmp(value, "0") == 0) {
            interval_s = 0;
        } else {
            char *end = NULL;
            const unsigned long parsed = strtoul(value, &end, 10);
            if (end == value || (end != NULL && *skip_spaces(end) != '\0') || parsed > UINT32_MAX) {
                err = ESP_ERR_INVALID_ARG;
            } else {
                interval_s = (uint32_t)parsed;
            }
        }
        if (err == ESP_OK) {
            err = nvs_set_auto_interval_s(interval_s);
        }
        printf("ota: auto %s interval_s=%u\n", esp_err_to_name(err), (unsigned)nvs_get_auto_interval_s());
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "fetch ", 6) == 0) {
        const char *cursor = sub + 6;
        char url[OTA_URL_MAX];
        char sha[OTA_SHA256_HEX_LEN + 1] = {};
        esp_err_t err = take_token(&cursor, url, sizeof(url)) && is_http_url(url) ? ESP_OK : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) {
            char maybe_sha[OTA_SHA256_HEX_LEN + 1];
            if (take_token(&cursor, maybe_sha, sizeof(maybe_sha))) {
                if (!is_sha256_hex(maybe_sha)) {
                    err = ESP_ERR_INVALID_ARG;
                } else {
                    strlcpy(sha, maybe_sha, sizeof(sha));
                }
            }
        }
        if (err == ESP_OK) {
            err = start_install_task(url, sha[0] != '\0' ? sha : NULL, 0, false);
        }
        printf("ota: fetch %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "file ", 5) == 0) {
        const char *cursor = sub + 5;
        char path_in[OTA_LOCAL_PATH_MAX];
        char resolved[OTA_LOCAL_PATH_MAX];
        char sha[OTA_SHA256_HEX_LEN + 1] = {};
        esp_err_t err = take_token(&cursor, path_in, sizeof(path_in)) ? ESP_OK : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) {
            err = resolve_local_ota_path(path_in, resolved, sizeof(resolved));
        }
        if (err == ESP_OK) {
            char maybe_sha[OTA_SHA256_HEX_LEN + 1];
            if (take_token(&cursor, maybe_sha, sizeof(maybe_sha))) {
                if (!is_sha256_hex(maybe_sha)) {
                    err = ESP_ERR_INVALID_ARG;
                } else {
                    strlcpy(sha, maybe_sha, sizeof(sha));
                }
            }
        }
        if (err == ESP_OK) {
            err = start_file_install_task(resolved, sha[0] != '\0' ? sha : NULL);
        }
        printf("ota: file %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "usb") == 0 || strncasecmp(sub, "usb ", 4) == 0) {
        const char *cursor = sub + 3;
        char resolved[OTA_LOCAL_PATH_MAX];
        char sha[OTA_SHA256_HEX_LEN + 1] = {};
        esp_err_t err = resolve_local_ota_path(OTA_LOCAL_DEFAULT_RELATIVE, resolved, sizeof(resolved));
        if (err == ESP_OK) {
            char maybe_sha[OTA_SHA256_HEX_LEN + 1];
            if (take_token(&cursor, maybe_sha, sizeof(maybe_sha))) {
                if (!is_sha256_hex(maybe_sha)) {
                    err = ESP_ERR_INVALID_ARG;
                } else {
                    strlcpy(sha, maybe_sha, sizeof(sha));
                }
            }
        }
        if (err == ESP_OK) {
            err = start_file_install_task(resolved, sha[0] != '\0' ? sha : NULL);
        }
        printf("ota: usb %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "manifest ", 9) == 0) {
        const char *cursor = sub + 9;
        char url[OTA_URL_MAX];
        esp_err_t err = take_token(&cursor, url, sizeof(url)) && is_http_url(url) ? ESP_OK : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) {
            err = start_manifest_task(url);
        }
        printf("ota: manifest %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "recovery ", 9) == 0) {
        const char *cursor = sub + 9;
        char url[OTA_URL_MAX];
        esp_err_t err = take_token(&cursor, url, sizeof(url)) && is_http_url(url) ? nvs_set_pending_url(url) :
                                                                                        ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) {
            err = reboot_to_factory();
        }
        printf("ota: recovery %s\n", esp_err_to_name(err));
        fflush(stdout);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
        return true;
    }

    printf("ota: unknown subcommand \"%s\" (try: ota help)\n", sub);
    fflush(stdout);
    return true;
}
