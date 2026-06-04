#include "faculty175_pocketwatch.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "faculty175_board.h"

static const char *TAG = "pocketwatch";

#define POCKETWATCH_NVS_NS "watch"
#define POCKETWATCH_NVS_BG "bg"

extern const uint8_t _binary_pocketwatch_default_rgb565_start[] asm("_binary_pocketwatch_default_rgb565_start");
extern const uint8_t _binary_pocketwatch_default_rgb565_end[] asm("_binary_pocketwatch_default_rgb565_end");

static bool s_bg_loaded;
static bool s_bg_enabled = true;

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

    const size_t bytes = (size_t)(_binary_pocketwatch_default_rgb565_end -
                                  _binary_pocketwatch_default_rgb565_start);
    const size_t expected = (size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_H * sizeof(uint16_t);
    if (bytes != expected) {
        ESP_LOGW(TAG, "background asset size mismatch: %u != %u", (unsigned)bytes, (unsigned)expected);
        return false;
    }

    faculty175_display_draw_rgb565((const uint16_t *)_binary_pocketwatch_default_rgb565_start,
                                   0,
                                   0,
                                   FACULTY175_LCD_W,
                                   FACULTY175_LCD_H);
    return true;
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
    fflush(stdout);
    return true;
}
