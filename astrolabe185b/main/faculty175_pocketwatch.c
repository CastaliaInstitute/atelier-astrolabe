#include "faculty175_pocketwatch.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"

#include "faculty175_board.h"
#include "faculty175_usb.h"

static const char *TAG = "pocketwatch";

#define POCKETWATCH_NVS_NS "watch"
#define POCKETWATCH_NVS_BG "bg"
#define POCKETWATCH_STORAGE_BASE "/bust_cache"
#define POCKETWATCH_STORAGE_PARTITION "storage"
#define POCKETWATCH_STORAGE_PATH POCKETWATCH_STORAGE_BASE "/pocketwatch_default.rgb565"
#define POCKETWATCH_SD_PATH "bust_cache/pocketwatch_default_360.rgb565"

static bool s_bg_loaded;
static bool s_bg_enabled = true;
static bool s_bg_storage_checked;
static bool s_bg_storage_ready;
static uint16_t *s_bg_pixels;

#define POCKETWATCH_DIAL_CX 233
#define POCKETWATCH_DIAL_CY 238
#define POCKETWATCH_NUMBER_R 168
#define POCKETWATCH_HIT_R 42
#define POCKETWATCH_SEQUENCE_TAP_MS 1500
#define POCKETWATCH_SEQUENCE_TOTAL_MS 4500
#define POCKETWATCH_SEQUENCE_LEN 3

static int s_seq_hours[POCKETWATCH_SEQUENCE_LEN];
static uint8_t s_seq_count;
static uint32_t s_seq_first_ms;
static uint32_t s_seq_last_ms;

typedef struct {
    int h1;
    int h2;
    int h3;
    const char *slug;
    const char *mnemonic;
} pocketwatch_profile_code_t;

/* Hour-ring codes (1-12 only). Each slug has a distinct mnemonic triple. */
static const pocketwatch_profile_code_t k_profile_codes[] = {
    {1, 2, 3, "fortune", "three-card spread"},
    {4, 4, 3, "secops", "HTTPS 443"},
    {1, 7, 5, "castalia", "1.75 face"},
    {1, 4, 5, "ocarina", "I-IV-V chords"},
    {2, 9, 5, "lunasay", "29.5-day moon"},
    {9, 8, 7, "cameo", "portrait reel"},
    {12, 11, 10, "default", "wind home"},
};

static bool background_storage_ready(void)
{
    if (s_bg_storage_checked) {
        return s_bg_storage_ready;
    }
    s_bg_storage_checked = true;

    const esp_vfs_spiffs_conf_t conf = {
        .base_path = POCKETWATCH_STORAGE_BASE,
        .partition_label = POCKETWATCH_STORAGE_PARTITION,
        .max_files = 12,
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "background SPIFFS mount failed: %s", esp_err_to_name(err));
        return false;
    }
    s_bg_storage_ready = true;
    return true;
}

static bool background_load_pixels(void)
{
    if (s_bg_pixels != NULL) {
        return true;
    }
    if (!background_storage_ready()) {
        return false;
    }

    char path[160];
    if (!faculty175_usb_resolve_asset_path(POCKETWATCH_SD_PATH, POCKETWATCH_STORAGE_PATH, path, sizeof(path))) {
        ESP_LOGW(TAG, "background asset missing");
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "background missing: %s", path);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    const long size = ftell(f);
    const size_t expected = (size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_H * sizeof(uint16_t);
    const size_t legacy = (size_t)466 * (size_t)466 * sizeof(uint16_t);
    if ((size != (long)expected && size != (long)legacy) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        ESP_LOGW(TAG, "background asset size mismatch: %ld", size);
        return false;
    }

    s_bg_pixels = (uint16_t *)heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_bg_pixels == NULL) {
        s_bg_pixels = (uint16_t *)heap_caps_malloc(expected, MALLOC_CAP_8BIT);
    }
    if (s_bg_pixels == NULL) {
        fclose(f);
        ESP_LOGW(TAG, "background alloc failed");
        return false;
    }

    if (size == (long)expected) {
        const size_t got = fread(s_bg_pixels, 1, expected, f);
        fclose(f);
        if (got != expected) {
            heap_caps_free(s_bg_pixels);
            s_bg_pixels = NULL;
            ESP_LOGW(TAG, "background short read: %u != %u", (unsigned)got, (unsigned)expected);
            return false;
        }
    } else {
        uint16_t *legacy_pixels = (uint16_t *)heap_caps_malloc(legacy, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (legacy_pixels == NULL) {
            legacy_pixels = (uint16_t *)heap_caps_malloc(legacy, MALLOC_CAP_8BIT);
        }
        if (legacy_pixels == NULL) {
            fclose(f);
            heap_caps_free(s_bg_pixels);
            s_bg_pixels = NULL;
            ESP_LOGW(TAG, "legacy background alloc failed");
            return false;
        }
        const size_t got = fread(legacy_pixels, 1, legacy, f);
        fclose(f);
        if (got != legacy) {
            heap_caps_free(s_bg_pixels);
            s_bg_pixels = NULL;
            heap_caps_free(legacy_pixels);
            ESP_LOGW(TAG, "legacy background short read: %u != %u", (unsigned)got, (unsigned)legacy);
            return false;
        }
        for (int y = 0; y < FACULTY175_LCD_H; ++y) {
            const int src_y = (int)((int64_t)y * 466 / FACULTY175_LCD_H);
            for (int x = 0; x < FACULTY175_LCD_W; ++x) {
                const int src_x = (int)((int64_t)x * 466 / FACULTY175_LCD_W);
                s_bg_pixels[(size_t)y * FACULTY175_LCD_W + (size_t)x] =
                    legacy_pixels[(size_t)src_y * 466u + (size_t)src_x];
            }
        }
        heap_caps_free(legacy_pixels);
    }

    ESP_LOGI(TAG, "background loaded: %s", path);
    return true;
}

static void profile_sequence_reset(void)
{
    s_seq_count = 0;
    s_seq_first_ms = 0;
    s_seq_last_ms = 0;
}

static bool profile_sequence_matches(const pocketwatch_profile_code_t *code)
{
    return s_seq_hours[0] == code->h1 && s_seq_hours[1] == code->h2 && s_seq_hours[2] == code->h3;
}

static const char *profile_slug_for_sequence(void)
{
    for (size_t i = 0; i < sizeof(k_profile_codes) / sizeof(k_profile_codes[0]); ++i) {
        if (profile_sequence_matches(&k_profile_codes[i])) {
            return k_profile_codes[i].slug;
        }
    }
    return NULL;
}

int faculty175_pocketwatch_hour_at(int16_t x, int16_t y)
{
    const int dx = (int)x - POCKETWATCH_DIAL_CX;
    const int dy = (int)y - POCKETWATCH_DIAL_CY;
    const int dist_sq = dx * dx + dy * dy;
    const int min_r = POCKETWATCH_NUMBER_R - POCKETWATCH_HIT_R;
    const int max_r = POCKETWATCH_NUMBER_R + POCKETWATCH_HIT_R;
    if (dist_sq < min_r * min_r || dist_sq > max_r * max_r) {
        return 0;
    }

    float angle = atan2f((float)dy, (float)dx) + 1.5707963f;
    if (angle < 0.0f) {
        angle += 6.2831853f;
    }
    int hour = (int)lrintf(angle * 12.0f / 6.2831853f) % 12;
    if (hour == 0) {
        hour = 12;
    }
    return hour;
}

bool faculty175_pocketwatch_profile_tap(int16_t x, int16_t y, uint32_t now_ms, char *out_slug, size_t out_cap)
{
    const int hour = faculty175_pocketwatch_hour_at(x, y);
    if (hour == 0) {
        profile_sequence_reset();
        return false;
    }

    if (s_seq_count > 0 &&
        (now_ms - s_seq_last_ms > POCKETWATCH_SEQUENCE_TAP_MS ||
         now_ms - s_seq_first_ms > POCKETWATCH_SEQUENCE_TOTAL_MS)) {
        profile_sequence_reset();
    }

    if (s_seq_count == 0) {
        s_seq_first_ms = now_ms;
    }

    if (s_seq_count < POCKETWATCH_SEQUENCE_LEN) {
        s_seq_hours[s_seq_count++] = hour;
    } else {
        s_seq_hours[0] = s_seq_hours[1];
        s_seq_hours[1] = s_seq_hours[2];
        s_seq_hours[2] = hour;
    }
    s_seq_last_ms = now_ms;

    if (s_seq_count < POCKETWATCH_SEQUENCE_LEN) {
        return false;
    }

    const char *slug = profile_slug_for_sequence();
    if (slug == NULL) {
        return false;
    }

    const int h1 = s_seq_hours[0];
    const int h2 = s_seq_hours[1];
    const int h3 = s_seq_hours[2];
    profile_sequence_reset();
    if (out_slug != NULL && out_cap > 0) {
        strncpy(out_slug, slug, out_cap - 1);
        out_slug[out_cap - 1] = '\0';
    }
    ESP_LOGI(TAG, "profile dial code %d-%d-%d -> %s", h1, h2, h3, slug);
    return true;
}

static void load_bg_setting(void)
{
    if (s_bg_loaded) {
        return;
    }
    s_bg_loaded = true;

    nvs_handle_t nvs;
    uint8_t enabled = 1;
    if (nvs_open(POCKETWATCH_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_u8(nvs, POCKETWATCH_NVS_BG, &enabled) == ESP_OK) {
            s_bg_enabled = enabled != 0;
        }
        nvs_close(nvs);
    }
}

bool faculty175_pocketwatch_background_enabled(void)
{
    load_bg_setting();
    return s_bg_enabled;
}

esp_err_t faculty175_pocketwatch_background_set_enabled(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(POCKETWATCH_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, POCKETWATCH_NVS_BG, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        s_bg_loaded = true;
        s_bg_enabled = enabled;
    }
    return err;
}

bool faculty175_pocketwatch_draw_background(void)
{
    if (!faculty175_pocketwatch_background_enabled()) {
        return false;
    }
    if (!background_load_pixels()) {
        return false;
    }

    faculty175_display_draw_rgb565(s_bg_pixels,
                                   0,
                                   0,
                                   FACULTY175_LCD_W,
                                   FACULTY175_LCD_H);
    return true;
}

const uint16_t *faculty175_pocketwatch_background_pixels(void)
{
    if (!background_load_pixels()) {
        return NULL;
    }
    return s_bg_pixels;
}

bool faculty175_pocketwatch_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "watch") != 0 && strncasecmp(line, "watch ", 6) != 0 &&
                         strcasecmp(line, "pocketwatch") != 0 && strncasecmp(line, "pocketwatch ", 12) != 0)) {
        return false;
    }

    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }

    if (*sub == '\0' || strcasecmp(sub, "status") == 0 || strcasecmp(sub, "bg") == 0) {
        printf("watch: background=%s source=flash\n",
               faculty175_pocketwatch_background_enabled() ? "on" : "off");
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "bg on") == 0 || strcasecmp(sub, "background on") == 0) {
        const esp_err_t err = faculty175_pocketwatch_background_set_enabled(true);
        printf("watch: background on %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "bg off") == 0 || strcasecmp(sub, "background off") == 0) {
        const esp_err_t err = faculty175_pocketwatch_background_set_enabled(false);
        printf("watch: background off %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }

    printf("watch commands:\n");
    printf("  watch status\n");
    printf("  watch bg on|off\n");
    printf("  profile dial (3 taps on hour ring, <=1.5s apart):\n");
    for (size_t i = 0; i < sizeof(k_profile_codes) / sizeof(k_profile_codes[0]); ++i) {
        const pocketwatch_profile_code_t *code = &k_profile_codes[i];
        printf("    %d-%d-%d %s (%s)\n", code->h1, code->h2, code->h3, code->slug, code->mnemonic);
    }
    fflush(stdout);
    return true;
}
