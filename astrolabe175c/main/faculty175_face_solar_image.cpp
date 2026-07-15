#include "faculty175_face_solar_image.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "astrolabe_time.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "faculty175_board.h"
#include "faculty175_wifi_settings.h"
#include "stb_image.h"

namespace {

constexpr const char *TAG = "faculty175_solar_image";
constexpr size_t kMaxPngBytes = 360000;
constexpr size_t kPixels = static_cast<size_t>(FACULTY175_LCD_W) * static_cast<size_t>(FACULTY175_LCD_H);
constexpr time_t kRefreshSeconds = 30 * 60;
constexpr time_t kFailureBackoffSeconds = 5 * 60;
constexpr const char *kCacheBasePath = "/bust_cache";

struct SolarChannel {
    int wavelength;
    const char *label;
    const char *cache_path;
    const char *meta_path;
};

constexpr SolarChannel kChannels[] = {
    {171, "AIA 171", "/bust_cache/solar_aia171.png", "/bust_cache/solar_aia171.meta"},
    {304, "AIA 304", "/bust_cache/solar_aia304.png", "/bust_cache/solar_aia304.meta"},
    {193, "AIA 193", "/bust_cache/solar_aia193.png", "/bust_cache/solar_aia193.meta"},
};

SemaphoreHandle_t s_mux = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_busy = false;
volatile bool s_force = false;
int s_channel = 0;
int s_requested_channel = 0;
int s_cached_channel = -1;
time_t s_cached_epoch = 0;
time_t s_last_failure_epoch = 0;
uint16_t *s_pixels = nullptr;
char s_error[48] = {};
bool s_flash_cache_checked[sizeof(kChannels) / sizeof(kChannels[0])] = {};

void set_error(const char *msg)
{
    if (msg == nullptr) {
        s_error[0] = '\0';
        return;
    }
    strlcpy(s_error, msg, sizeof(s_error));
}

bool mux_take(uint32_t ms)
{
    if (s_mux == nullptr) {
        s_mux = xSemaphoreCreateMutex();
    }
    return s_mux != nullptr && xSemaphoreTake(s_mux, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void mux_give()
{
    if (s_mux != nullptr) {
        xSemaphoreGive(s_mux);
    }
}

void free_cached_locked()
{
    heap_caps_free(s_pixels);
    s_pixels = nullptr;
    s_cached_channel = -1;
    s_cached_epoch = 0;
}

bool utc_timestamp(char *out, size_t cap)
{
    if (out == nullptr || cap == 0 || !astrolabe_time_valid()) {
        return false;
    }
    const time_t now = astrolabe_time_now();
    struct tm utc = {};
    gmtime_r(&now, &utc);
    return strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &utc) > 0;
}

bool build_url(int channel, char *out, size_t cap)
{
    char date[24];
    if (!utc_timestamp(date, sizeof(date))) {
        set_error("time pending");
        return false;
    }
    if (channel < 0 || channel >= static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0]))) {
        channel = 0;
    }
    const int n = snprintf(out,
                           cap,
                           "https://api.helioviewer.org/v2/takeScreenshot/"
                           "?date=%s&imageScale=5.15&layers=%%5BSDO,AIA,AIA,%d,1,100%%5D"
                           "&x1=-1200&y1=-1200&x2=1200&y2=1200&width=466&height=466"
                           "&display=true&watermark=false",
                           date,
                           kChannels[channel].wavelength);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool ensure_cache_fs()
{
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = kCacheBasePath;
    conf.partition_label = "storage";
    conf.max_files = 4;
    conf.format_if_mount_failed = false;
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    return err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}

bool read_flash_epoch(const char *path, time_t *out)
{
    if (out != nullptr) {
        *out = 0;
    }
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    char buf[32] = {};
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) {
        return false;
    }
    char *end = nullptr;
    const long long epoch = strtoll(buf, &end, 10);
    if (epoch <= 0) {
        return false;
    }
    if (out != nullptr) {
        *out = static_cast<time_t>(epoch);
    }
    return true;
}

bool write_flash_epoch(const char *path, time_t epoch)
{
    char tmp[96];
    const int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(tmp)) {
        return false;
    }
    FILE *f = fopen(tmp, "wb");
    if (f == nullptr) {
        return false;
    }
    fprintf(f, "%lld\n", static_cast<long long>(epoch));
    const int close_rc = fclose(f);
    if (close_rc != 0) {
        unlink(tmp);
        return false;
    }
    unlink(path);
    return rename(tmp, path) == 0;
}

bool write_flash_png(int channel, const uint8_t *png, size_t png_len, time_t epoch)
{
    if (!ensure_cache_fs() || png == nullptr || png_len < 8 || png_len > kMaxPngBytes ||
        channel < 0 || channel >= static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0]))) {
        return false;
    }
    const char *path = kChannels[channel].cache_path;
    char tmp[96];
    const int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(tmp)) {
        return false;
    }
    FILE *f = fopen(tmp, "wb");
    if (f == nullptr) {
        return false;
    }
    const size_t wrote = fwrite(png, 1, png_len, f);
    const int close_rc = fclose(f);
    if (wrote != png_len || close_rc != 0) {
        unlink(tmp);
        return false;
    }
    unlink(path);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return write_flash_epoch(kChannels[channel].meta_path, epoch);
}

bool download_png(const char *url, uint8_t **out, size_t *out_len)
{
    *out = nullptr;
    *out_len = 0;

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 30000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.keep_alive_enable = false;
    cfg.buffer_size = 4096;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        set_error("http alloc");
        return false;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Solar/1");
    esp_http_client_set_header(client, "Accept", "image/png,*/*;q=0.1");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        set_error("http open");
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t header_len = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200 || header_len > static_cast<int64_t>(kMaxPngBytes)) {
        set_error("HTTP image");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const size_t alloc_len = header_len > 0 ? static_cast<size_t>(header_len) : kMaxPngBytes;
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(alloc_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint8_t *>(heap_caps_malloc(alloc_len, MALLOC_CAP_8BIT));
    }
    if (buf == nullptr) {
        set_error("png alloc");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    size_t total = 0;
    while (total < alloc_len) {
        const int n = esp_http_client_read(client,
                                           reinterpret_cast<char *>(buf + total),
                                           static_cast<int>(alloc_len - total));
        if (n < 0) {
            set_error("png read");
            heap_caps_free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (n == 0) {
            break;
        }
        total += static_cast<size_t>(n);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total < 8 || (header_len > 0 && total != static_cast<size_t>(header_len)) || total == alloc_len) {
        set_error("short png");
        heap_caps_free(buf);
        return false;
    }
    *out = buf;
    *out_len = total;
    return true;
}

bool decode_png(uint8_t *png, size_t png_len, int channel, time_t cached_epoch)
{
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc *rgba = stbi_load_from_memory(png, static_cast<int>(png_len), &w, &h, &comp, 4);
    if (rgba == nullptr) {
        set_error("png open");
        return false;
    }
    if (w != FACULTY175_LCD_W || h != FACULTY175_LCD_H) {
        stbi_image_free(rgba);
        set_error("png size");
        return false;
    }

    auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(kPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pixels == nullptr) {
        stbi_image_free(rgba);
        set_error("image alloc");
        return false;
    }

    for (size_t i = 0; i < kPixels; ++i) {
        const uint8_t *src = &rgba[i * 4];
        pixels[i] = faculty175_display_rgb888(src[0], src[1], src[2]);
    }
    stbi_image_free(rgba);

    if (!mux_take(3000)) {
        heap_caps_free(pixels);
        set_error("image lock");
        return false;
    }
    free_cached_locked();
    s_pixels = pixels;
    s_cached_channel = channel;
    s_cached_epoch = cached_epoch > 0 ? cached_epoch : (astrolabe_time_valid() ? astrolabe_time_now() : time(nullptr));
    mux_give();
    set_error(nullptr);
    return true;
}

bool load_flash_cache(int channel)
{
    if (channel < 0 || channel >= static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0]))) {
        channel = 0;
    }
    s_flash_cache_checked[channel] = true;
    if (!ensure_cache_fs()) {
        set_error("cache fs");
        return false;
    }
    struct stat st = {};
    if (stat(kChannels[channel].cache_path, &st) != 0 || st.st_size < 8 || st.st_size > static_cast<off_t>(kMaxPngBytes)) {
        set_error("no flash cache");
        return false;
    }
    FILE *f = fopen(kChannels[channel].cache_path, "rb");
    if (f == nullptr) {
        set_error("cache open");
        return false;
    }
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(st.st_size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(st.st_size), MALLOC_CAP_8BIT));
    }
    if (buf == nullptr) {
        fclose(f);
        set_error("cache alloc");
        return false;
    }
    const size_t got = fread(buf, 1, static_cast<size_t>(st.st_size), f);
    fclose(f);
    if (got != static_cast<size_t>(st.st_size)) {
        heap_caps_free(buf);
        set_error("cache read");
        return false;
    }
    time_t epoch = 0;
    (void)read_flash_epoch(kChannels[channel].meta_path, &epoch);
    const bool ok = decode_png(buf, got, channel, epoch);
    heap_caps_free(buf);
    if (ok) {
        ESP_LOGI(TAG, "loaded %s from flash cache", kChannels[channel].label);
    }
    return ok;
}

void fetch_task(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int channel = s_requested_channel;
        if (s_pixels == nullptr && channel >= 0 &&
            channel < static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0])) &&
            !s_flash_cache_checked[channel]) {
            (void)load_flash_cache(channel);
        }
        char url[256];
        bool ok = build_url(channel, url, sizeof(url));
        uint8_t *png = nullptr;
        size_t png_len = 0;
        if (ok) {
            ok = download_png(url, &png, &png_len);
        }
        if (ok) {
            const time_t epoch = astrolabe_time_valid() ? astrolabe_time_now() : time(nullptr);
            const bool saved = write_flash_png(channel, png, png_len, epoch);
            ok = decode_png(png, png_len, channel, epoch);
            ESP_LOGI(TAG, "%s flash cache %s bytes=%u",
                     saved ? "saved" : "skipped",
                     kChannels[channel].label,
                     static_cast<unsigned>(png_len));
        }
        heap_caps_free(png);
        if (!ok) {
            s_last_failure_epoch = astrolabe_time_valid() ? astrolabe_time_now() : time(nullptr);
        }
        ESP_LOGI(TAG, "%s %s %s", ok ? "cached" : "failed", kChannels[channel].label, s_error[0] ? s_error : "ok");
        s_force = false;
        s_busy = false;
    }
}

void ensure_task()
{
    if (s_task != nullptr) {
        return;
    }
    if (xTaskCreate(fetch_task, "solar_img", 12288, nullptr, 2, &s_task) != pdPASS) {
        s_task = nullptr;
        set_error("task alloc");
    }
}

bool cache_stale(time_t now)
{
    return s_cached_epoch <= 0 || (now > s_cached_epoch && now - s_cached_epoch > kRefreshSeconds);
}

}  // namespace

extern "C" void faculty175_solar_image_request(bool force)
{
    if (s_busy) {
        return;
    }
    if (s_pixels == nullptr && s_channel >= 0 &&
        s_channel < static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0])) &&
        !s_flash_cache_checked[s_channel]) {
        (void)load_flash_cache(s_channel);
    }
    if (!faculty175_wifi_settings_sta_connected()) {
        set_error("wifi pending");
        return;
    }
    const time_t now = astrolabe_time_valid() ? astrolabe_time_now() : time(nullptr);
    if (!force && s_last_failure_epoch > 0 && now > s_last_failure_epoch &&
        now - s_last_failure_epoch < kFailureBackoffSeconds) {
        return;
    }
    if (!force && s_cached_channel == s_channel && !cache_stale(now)) {
        return;
    }
    ensure_task();
    if (s_task == nullptr) {
        return;
    }
    s_requested_channel = s_channel;
    s_force = force;
    s_busy = true;
    xTaskNotify(s_task, 1, eSetBits);
}

extern "C" bool faculty175_solar_image_draw_cached(void)
{
    if (!mux_take(20)) {
        return false;
    }
    if (s_pixels == nullptr) {
        mux_give();
        return false;
    }
    faculty175_display_draw_rgb565(s_pixels, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);
    mux_give();
    return true;
}

extern "C" bool faculty175_solar_image_copy_cached(uint16_t *out, size_t pixel_count, time_t *cached_epoch_out)
{
    if (out == nullptr || pixel_count < kPixels || !mux_take(20)) {
        return false;
    }
    if (s_pixels == nullptr) {
        mux_give();
        return false;
    }
    memcpy(out, s_pixels, kPixels * sizeof(uint16_t));
    if (cached_epoch_out != nullptr) {
        *cached_epoch_out = s_cached_epoch;
    }
    mux_give();
    return true;
}

extern "C" bool faculty175_solar_image_busy(void)
{
    return s_busy;
}

extern "C" bool faculty175_solar_image_has_cached(void)
{
    return s_pixels != nullptr;
}

extern "C" bool faculty175_solar_image_action(uint32_t seed_ms)
{
    (void)seed_ms;
    if (s_busy) {
        return false;
    }
    s_channel = (s_channel + 1) % static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0]));
    faculty175_solar_image_request(true);
    return true;
}

extern "C" const char *faculty175_solar_image_error(void)
{
    return s_error;
}

extern "C" const char *faculty175_solar_image_channel(void)
{
    if (s_channel < 0 || s_channel >= static_cast<int>(sizeof(kChannels) / sizeof(kChannels[0]))) {
        return kChannels[0].label;
    }
    return kChannels[s_channel].label;
}

extern "C" time_t faculty175_solar_image_cached_epoch(void)
{
    return s_cached_epoch;
}
