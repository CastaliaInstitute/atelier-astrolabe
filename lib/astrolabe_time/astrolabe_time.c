#include "astrolabe_time.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static const char *TAG = "astrolabe_time";

#define TIME_NVS_NS "time"
#define TIME_NVS_TZ "tz"
#define TIME_NVS_EPOCH "epoch"
#define TIME_DEFAULT_TZ "UTC0"

static bool s_started;
static bool s_synced;
static int64_t s_last_sync_epoch;
static uint32_t s_retry_count;
static bool s_tz_loaded;
static bool s_epoch_loaded;
static char s_tz[ASTROLABE_TIME_TZ_MAX_LEN + 1] = TIME_DEFAULT_TZ;

static bool valid_tz(const char *tz)
{
    if (tz == NULL || tz[0] == '\0' || strlen(tz) > ASTROLABE_TIME_TZ_MAX_LEN) {
        return false;
    }
    for (const char *p = tz; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c >= 0x7f || c == '"' || c == '\\') {
            return false;
        }
    }
    return true;
}

static void apply_tz(const char *tz)
{
    setenv("TZ", valid_tz(tz) ? tz : TIME_DEFAULT_TZ, 1);
    tzset();
}

static void load_tz_from_nvs(void)
{
    if (s_tz_loaded) {
        apply_tz(s_tz);
        return;
    }
    strncpy(s_tz, TIME_DEFAULT_TZ, sizeof(s_tz) - 1);
    nvs_handle_t nvs;
    if (nvs_open(TIME_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_tz);
        if (nvs_get_str(nvs, TIME_NVS_TZ, s_tz, &len) != ESP_OK || !valid_tz(s_tz)) {
            strncpy(s_tz, TIME_DEFAULT_TZ, sizeof(s_tz) - 1);
            s_tz[sizeof(s_tz) - 1] = '\0';
        }
        nvs_close(nvs);
    }
    s_tz_loaded = true;
    apply_tz(s_tz);
}

static void save_epoch_to_nvs(time_t epoch)
{
    if (epoch < (time_t)ASTROLABE_TIME_VALID_MIN_EPOCH) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(TIME_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        if (nvs_set_i64(nvs, TIME_NVS_EPOCH, (int64_t)epoch) == ESP_OK) {
            (void)nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
}

static void load_epoch_from_nvs(void)
{
    if (s_epoch_loaded) {
        return;
    }
    s_epoch_loaded = true;
    if (time(NULL) >= (time_t)ASTROLABE_TIME_VALID_MIN_EPOCH) {
        return;
    }
    nvs_handle_t nvs;
    int64_t saved_epoch = 0;
    if (nvs_open(TIME_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        (void)nvs_get_i64(nvs, TIME_NVS_EPOCH, &saved_epoch);
        nvs_close(nvs);
    }
    if (saved_epoch >= ASTROLABE_TIME_VALID_MIN_EPOCH) {
        const struct timeval tv = {
            .tv_sec = (time_t)saved_epoch,
            .tv_usec = 0,
        };
        if (settimeofday(&tv, NULL) == 0) {
            s_last_sync_epoch = saved_epoch;
            ESP_LOGI(TAG, "restored saved epoch=%lld", (long long)saved_epoch);
        }
    }
}

static void time_sync_cb(struct timeval *tv)
{
    const time_t epoch = tv != NULL ? tv->tv_sec : time(NULL);
    s_synced = epoch >= (time_t)ASTROLABE_TIME_VALID_MIN_EPOCH;
    s_last_sync_epoch = (int64_t)epoch;
    if (s_synced) {
        save_epoch_to_nvs(epoch);
        char stamp[32];
        (void)astrolabe_time_format_utc(stamp, sizeof(stamp));
        ESP_LOGI(TAG, "synced utc=%s", stamp);
    } else {
        ESP_LOGW(TAG, "sync callback epoch still stale: %lld", (long long)epoch);
    }
}

static const char *server_or_default(const astrolabe_time_config_t *config)
{
    if (config != NULL && config->server != NULL && config->server[0] != '\0') {
        return config->server;
    }
    return "time.google.com";
}

esp_err_t astrolabe_time_start(const astrolabe_time_config_t *config)
{
    load_tz_from_nvs();

    if (!s_started) {
        const char *server = server_or_default(config);
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
        sntp.sync_cb = time_sync_cb;
        sntp.wait_for_sync = true;
        sntp.start = true;
        const esp_err_t err = esp_netif_sntp_init(&sntp);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
            return err;
        }
        s_started = true;
        ESP_LOGI(TAG, "SNTP started server=%s", server);
    } else if (!astrolabe_time_valid()) {
        const esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SNTP restart failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    const uint32_t wait_ms = config != NULL ? config->sync_wait_ms : 0;
    if (wait_ms > 0 && !astrolabe_time_valid()) {
        const esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(wait_ms));
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "SNTP wait failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    s_synced = astrolabe_time_valid();
    return ESP_OK;
}

esp_err_t astrolabe_time_retry_if_stale(void)
{
    if (astrolabe_time_valid()) {
        s_synced = true;
        return ESP_OK;
    }
    ++s_retry_count;
    return astrolabe_time_start(NULL);
}

bool astrolabe_time_valid(void)
{
    load_epoch_from_nvs();
    return time(NULL) >= (time_t)ASTROLABE_TIME_VALID_MIN_EPOCH;
}

time_t astrolabe_time_now(void)
{
    load_epoch_from_nvs();
    return time(NULL);
}

void astrolabe_time_utc(struct tm *out_tm)
{
    if (out_tm == NULL) {
        return;
    }
    load_epoch_from_nvs();
    const time_t now = time(NULL);
    gmtime_r(&now, out_tm);
}

void astrolabe_time_local(struct tm *out_tm)
{
    if (out_tm == NULL) {
        return;
    }
    load_epoch_from_nvs();
    load_tz_from_nvs();
    const time_t now = time(NULL);
    localtime_r(&now, out_tm);
}

size_t astrolabe_time_format_utc(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    struct tm utc = {};
    astrolabe_time_utc(&utc);
    return strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

size_t astrolabe_time_format_local(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    struct tm local = {};
    astrolabe_time_local(&local);
    return strftime(out, cap, "%Y-%m-%dT%H:%M:%S%z", &local);
}

const char *astrolabe_time_timezone(void)
{
    load_tz_from_nvs();
    return s_tz;
}

esp_err_t astrolabe_time_set_timezone(const char *tz)
{
    if (!valid_tz(tz)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(TIME_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, TIME_NVS_TZ, tz);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        strncpy(s_tz, tz, sizeof(s_tz) - 1);
        s_tz[sizeof(s_tz) - 1] = '\0';
        s_tz_loaded = true;
        apply_tz(s_tz);
        ESP_LOGI(TAG, "timezone set tz=%s", s_tz);
    }
    return err;
}

esp_err_t astrolabe_time_set_epoch(time_t epoch)
{
    if (epoch < (time_t)ASTROLABE_TIME_VALID_MIN_EPOCH) {
        return ESP_ERR_INVALID_ARG;
    }
    const struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    s_synced = true;
    s_last_sync_epoch = (int64_t)epoch;
    save_epoch_to_nvs(epoch);
    ESP_LOGI(TAG, "manual time set epoch=%lld", (long long)epoch);
    return ESP_OK;
}

void astrolabe_time_status(astrolabe_time_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->started = s_started;
    out->synced = s_synced || astrolabe_time_valid();
    out->epoch = time(NULL);
    out->last_sync_epoch = s_last_sync_epoch;
    out->retry_count = s_retry_count;
    strncpy(out->tz, astrolabe_time_timezone(), sizeof(out->tz) - 1);
}
