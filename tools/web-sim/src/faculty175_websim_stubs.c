#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <emscripten.h>

#include "astrolabe_time.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_netif_ip_addr.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "faculty175_almanac.h"
#include "faculty175_apocalypso.h"
#include "faculty175_board.h"
#include "faculty175_ble.h"
#include "faculty175_charts.h"
#include "faculty175_device_settings.h"
#include "faculty175_face_native.h"
#include "faculty175_face_tarot_image.h"
#include "faculty175_face_tarot_spiffs_image.h"
#include "faculty175_faces.h"
#include "faculty175_faculty.h"
#include "faculty175_lvgl.h"
#include "faculty175_quotes.h"
#include "faculty175_rocket.h"
#include "faculty175_touch.h"
#include "faculty175_wifi_settings.h"
#include "faculty175_wifi_lab.h"
#include "faculty175_wifi_monitor.h"
#if __has_include("faculty175_usb_screen.h")
#include "faculty175_usb_screen.h"
#endif
#if __has_include("faculty175_rotary_state.h")
#include "faculty175_rotary_state.h"
#else
#define FACULTY175_ROTARY_STATE_KIND_COUNT 8
typedef struct {
    bool valid;
    uint8_t state;
    int rssi_dbm;
    const char *source;
    uint32_t age_ms;
    bool has_style;
    uint8_t facial_hair;
    uint8_t glasses;
    uint8_t skin_tone;
    uint8_t hair_color;
    uint8_t eye_color;
} faculty175_rotary_state_t;
#endif
#include "freertos/task.h"
#include "nvs.h"

static uint16_t s_fb[FACULTY175_LCD_W * FACULTY175_LCD_H];
static bool s_flush_suspended;
static uint32_t s_tick_ms;

const uint8_t _binary_pocketwatch_default_rgb565_start[FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t)] = {0};
const uint8_t _binary_maze_466_png_start[1] = {0};
const uint8_t _binary_maze_466_png_end[1] = {0};

EM_JS(void, js_canvas_init, (int width, int height), {
  const canvas = document.querySelector('#astrolabe-canvas');
  canvas.width = width;
  canvas.height = height;
  Module.astrolabeCtx = canvas.getContext('2d', { alpha: false });
  Module.astrolabeImageData = Module.astrolabeCtx.createImageData(width, height);
});

EM_JS(void, js_canvas_flush_rgb565, (const uint16_t *src, int width, int height), {
  const image = Module.astrolabeImageData;
  const data = image.data;
  for (let i = 0, j = 0; i < width * height; ++i) {
    const p = HEAPU16[(src >> 1) + i];
    data[j++] = ((p >> 11) & 0x1f) * 255 / 31;
    data[j++] = ((p >> 5) & 0x3f) * 255 / 63;
    data[j++] = (p & 0x1f) * 255 / 31;
    data[j++] = 255;
  }
  Module.astrolabeCtx.putImageData(image, 0, 0);
});

void faculty175_websim_display_init(void)
{
    js_canvas_init(FACULTY175_LCD_W, FACULTY175_LCD_H);
    faculty175_display_fill_rgb565(0);
    faculty175_display_flush();
}

void faculty175_websim_display_tick(uint32_t elapsed_ms)
{
    s_tick_ms += elapsed_ms;
}

uint16_t faculty175_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xf8) << 8) | ((uint16_t)(g & 0xf8) << 3) | ((uint16_t)b >> 3));
}

uint16_t faculty175_display_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_pack_rgb888(r, g, b);
}

uint16_t faculty175_display_fb_from_logical565(uint16_t logical565)
{
    return logical565;
}

uint32_t faculty175_display_bkgd_u32(void)
{
    return 0;
}

static void put_px(int x, int y, uint16_t color)
{
    if ((unsigned)x >= FACULTY175_LCD_W || (unsigned)y >= FACULTY175_LCD_H) {
        return;
    }
    s_fb[y * FACULTY175_LCD_W + x] = color;
}

void faculty175_display_draw_pixel(int x, int y, uint16_t color)
{
    put_px(x, y, color);
}

void faculty175_display_fill_rgb565(uint16_t color)
{
    for (size_t i = 0; i < sizeof(s_fb) / sizeof(s_fb[0]); ++i) {
        s_fb[i] = color;
    }
}

void faculty175_display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > FACULTY175_LCD_W ? FACULTY175_LCD_W : x + w;
    int y1 = y + h > FACULTY175_LCD_H ? FACULTY175_LCD_H : y + h;
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            put_px(xx, yy, color);
        }
    }
}

void faculty175_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    if (pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            put_px(x + xx, y + yy, pixels[yy * w + xx]);
        }
    }
}

void faculty175_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        put_px(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void faculty175_display_draw_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    int x = -r;
    int y = 0;
    int err = 2 - 2 * r;
    do {
        put_px(cx - x, cy + y, color);
        put_px(cx - y, cy - x, color);
        put_px(cx + x, cy - y, color);
        put_px(cx + y, cy + x, color);
        int e = err;
        if (e <= y) {
            err += ++y * 2 + 1;
        }
        if (e > x || err > y) {
            err += ++x * 2 + 1;
        }
    } while (x < 0);
}

void faculty175_display_fill_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    const int rr = r * r;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= rr) {
                put_px(cx + x, cy + y, color);
            }
        }
    }
}

static uint8_t glyph_bits(char ch, int row)
{
    uint8_t v = (uint8_t)ch;
    v ^= (uint8_t)(row * 37u);
    v ^= (uint8_t)(v >> 3);
    return (uint8_t)(0x11u | (v & 0x1eu));
}

void faculty175_display_draw_text(const char *text, int x, int y, uint16_t color)
{
    if (text == NULL) {
        return;
    }
    for (int i = 0; text[i] != '\0'; ++i) {
        if (text[i] == ' ') {
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph_bits(text[i], row);
            for (int col = 0; col < 5; ++col) {
                if ((bits & (1u << (4 - col))) != 0) {
                    put_px(x + i * 6 + col, y + row, color);
                }
            }
        }
    }
}

void faculty175_display_draw_centered_text(const char *text, int y, uint16_t color)
{
    const int w = text != NULL ? (int)strlen(text) * 6 : 0;
    faculty175_display_draw_text(text, (FACULTY175_LCD_W - w) / 2, y, color);
}

void faculty175_display_draw_bezel_label(const char *text, bool bottom, int radius, uint32_t scroll_ms, uint16_t color)
{
    (void)radius;
    (void)scroll_ms;
    faculty175_display_draw_centered_text(text, bottom ? 424 : 28, color);
}

void faculty175_display_flush(void)
{
    if (!s_flush_suspended) {
        js_canvas_flush_rgb565(s_fb, FACULTY175_LCD_W, FACULTY175_LCD_H);
    }
}

void faculty175_display_flush_rect(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    faculty175_display_flush();
}

void faculty175_display_flush_suspended_set(bool suspended)
{
    s_flush_suspended = suspended;
}

size_t faculty175_display_frame_pixel_count(void)
{
    return sizeof(s_fb) / sizeof(s_fb[0]);
}

bool faculty175_display_frame_copy(uint16_t *out, size_t pixel_count)
{
    if (out == NULL || pixel_count < faculty175_display_frame_pixel_count()) {
        return false;
    }
    memcpy(out, s_fb, sizeof(s_fb));
    return true;
}

void faculty175_display_draw_pocketwatch(const char *detail, uint32_t anim_ms, bool boot_mode)
{
    (void)boot_mode;
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const uint16_t gold = faculty175_display_rgb888(226, 190, 112);
    const uint16_t fg = faculty175_display_rgb888(242, 238, 220);
    faculty175_display_fill_rgb565(faculty175_display_rgb888(7, 9, 13));
    faculty175_display_draw_circle(cx, cy, 216, faculty175_display_rgb888(58, 46, 30));
    faculty175_display_draw_circle(cx, cy, 184, gold);
    for (int i = 0; i < 12; ++i) {
        float a = ((float)i / 12.0f) * 6.2831853f - 1.5707963f;
        int x0 = cx + (int)lrintf(cosf(a) * 160.0f);
        int y0 = cy + (int)lrintf(sinf(a) * 160.0f);
        int x1 = cx + (int)lrintf(cosf(a) * 178.0f);
        int y1 = cy + (int)lrintf(sinf(a) * 178.0f);
        faculty175_display_draw_line(x0, y0, x1, y1, gold);
    }
    float sec = ((float)(anim_ms % 60000u) / 60000.0f) * 6.2831853f - 1.5707963f;
    faculty175_display_draw_line(cx, cy, cx + (int)lrintf(cosf(sec) * 150.0f), cy + (int)lrintf(sinf(sec) * 150.0f), gold);
    faculty175_display_draw_line(cx, cy, cx + 104, cy - 18, fg);
    faculty175_display_draw_line(cx, cy, cx - 46, cy - 86, fg);
    faculty175_display_fill_circle(cx, cy, 10, gold);
    faculty175_display_draw_centered_text(detail != NULL ? detail : "READY", 392, gold);
    faculty175_display_flush();
}

void faculty175_display_draw_status(faculty175_ui_state_t state, const char *faculty_name, const char *detail,
                                    uint32_t anim_ms, const uint8_t *waveform, const uint8_t *waveform_stream,
                                    size_t waveform_len)
{
    (void)state;
    (void)anim_ms;
    (void)waveform;
    (void)waveform_stream;
    (void)waveform_len;
    faculty175_display_fill_rgb565(faculty175_display_rgb888(8, 10, 16));
    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 214,
                                   faculty175_display_rgb888(64, 86, 100));
    faculty175_display_draw_centered_text(faculty_name != NULL ? faculty_name : "FACULTY", 190,
                                          faculty175_display_rgb888(255, 224, 138));
    faculty175_display_draw_centered_text(detail != NULL ? detail : "WEB SIM", 224,
                                          faculty175_display_rgb888(150, 190, 220));
    faculty175_display_flush();
}

void faculty175_display_frame_compose_carousel(const uint16_t *from, const uint16_t *to, int shift_px) { (void)from; (void)to; (void)shift_px; }
void faculty175_display_frame_compose_vertical(const uint16_t *from, const uint16_t *to, int shift_px) { (void)from; (void)to; (void)shift_px; }
void faculty175_display_frame_compose_radial(const uint16_t *from, const uint16_t *to, int radius_px) { (void)from; (void)to; (void)radius_px; }
void faculty175_display_frame_compose_nav_preview(const uint16_t *center, const uint16_t *left, const uint16_t *right, const uint16_t *up, const uint16_t *down) { (void)center; (void)left; (void)right; (void)up; (void)down; }
void faculty175_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h) { (void)opaque; faculty175_display_draw_rgb565(pixels, x, y, w, h); }
void faculty175_display_boot_progress(const char *detail, uint8_t step, uint8_t total, bool active) { (void)step; (void)total; (void)active; faculty175_display_draw_status(FACULTY175_UI_BOOT, "BOOT", detail, s_tick_ms, NULL, NULL, 0); }
void faculty175_display_waveform_update(const uint8_t *waveform, const uint8_t *waveform_stream, size_t waveform_len, bool visible) { (void)waveform; (void)waveform_stream; (void)waveform_len; (void)visible; }
void faculty175_display_nav_mode_set(bool enabled) { (void)enabled; }
void faculty175_display_touch_visual_update(int16_t x, int16_t y, bool down, uint32_t now_ms) { (void)x; (void)y; (void)down; (void)now_ms; }
void faculty175_display_lock(void) {}
void faculty175_display_unlock(void) {}
size_t faculty175_display_bmp_size(void) { return 0; }
int faculty175_display_write_bmp(FILE *out) { (void)out; return 0; }
size_t faculty175_display_bmp565_size(void) { return 0; }
esp_err_t faculty175_display_write_bmp565(faculty175_display_write_cb_t write_cb, void *ctx) { (void)write_cb; (void)ctx; return ESP_FAIL; }

esp_err_t faculty175_lvgl_init(void) { return ESP_FAIL; }
bool faculty175_lvgl_ready(void) { return false; }
void faculty175_lvgl_service(uint32_t now_ms) { (void)now_ms; }
bool faculty175_lvgl_face_supported(faculty175_face_id_t id) { (void)id; return false; }
bool faculty175_lvgl_draw_face(faculty175_face_id_t id, uint32_t anim_ms) { (void)id; (void)anim_ms; return false; }
bool faculty175_lvgl_draw_native_face(const faculty175_native_face_t *face, uint32_t anim_ms) { (void)face; (void)anim_ms; return false; }
bool faculty175_lvgl_draw_nav(const faculty175_face_desc_t *center,
                              const faculty175_face_desc_t *left,
                              const faculty175_face_desc_t *right,
                              const faculty175_face_desc_t *up,
                              const faculty175_face_desc_t *down,
                              uint32_t anim_ms)
{
    (void)center;
    (void)left;
    (void)right;
    (void)up;
    (void)down;
    (void)anim_ms;
    return false;
}
bool faculty175_lvgl_transition_nav(const faculty175_face_desc_t *center, bool vertical, int delta, uint32_t duration_ms)
{
    (void)center;
    (void)vertical;
    (void)delta;
    (void)duration_ms;
    return false;
}
bool faculty175_lvgl_transition_face(faculty175_face_id_t from_id,
                                     faculty175_face_id_t to_id,
                                     uint32_t anim_ms,
                                     bool vertical,
                                     int delta,
                                     uint32_t duration_ms,
                                     bool *animated_out)
{
    (void)from_id;
    (void)to_id;
    (void)anim_ms;
    (void)vertical;
    (void)delta;
    (void)duration_ms;
    if (animated_out) {
        *animated_out = false;
    }
    return false;
}
bool faculty175_lvgl_faces_share_transition_screen(faculty175_face_id_t a, faculty175_face_id_t b)
{
    (void)a;
    (void)b;
    return false;
}
bool faculty175_lvgl_animate_frames(const uint16_t *from,
                                    const uint16_t *to,
                                    bool vertical,
                                    int delta,
                                    uint32_t duration_ms)
{
    (void)from;
    (void)to;
    (void)vertical;
    (void)delta;
    (void)duration_ms;
    return false;
}

static void websim_placeholder_face(const char *label, uint32_t anim_ms)
{
    faculty175_display_draw_status(FACULTY175_UI_LISTEN, label, "websim stub", anim_ms, NULL, NULL, 0);
}

void faculty175_face_native_draw(const faculty175_native_face_t *face, uint32_t anim_ms)
{
    const char *label = (face != NULL && face->title != NULL) ? face->title : "Native";
    websim_placeholder_face(label, anim_ms);
}

bool faculty175_face_native_action(faculty175_face_id_t id, uint32_t seed_ms)
{
    (void)id;
    (void)seed_ms;
    return false;
}

bool faculty175_face_native_audio_busy(void)
{
    return false;
}

bool faculty175_face_native_chakra_delta(faculty175_face_id_t id, int delta)
{
    (void)id;
    (void)delta;
    return false;
}

const char *faculty175_face_native_chakra_name(void)
{
    return "chakra";
}

void faculty175_face_hid_draw(uint32_t anim_ms)
{
    websim_placeholder_face("HID", anim_ms);
}

void faculty175_usb_screen_draw_face(uint32_t anim_ms)
{
    websim_placeholder_face("USB Screen", anim_ms);
}

void faculty175_face_deathstar_draw(uint32_t anim_ms) { websim_placeholder_face("Death Star", anim_ms); }
void faculty175_face_maze_draw(uint32_t anim_ms) { websim_placeholder_face("Maze", anim_ms); }
void faculty175_face_watcher_draw(uint32_t anim_ms) { websim_placeholder_face("Watcher", anim_ms); }
void faculty175_face_wifilab_draw(faculty175_face_id_t id, uint32_t anim_ms)
{
    const faculty175_face_desc_t *face = faculty175_faces_get(id);
    websim_placeholder_face(face != NULL ? face->label : "Wi-Fi Lab", anim_ms);
}
bool faculty175_face_wifilab_action(faculty175_face_id_t id, uint32_t seed_ms)
{
    (void)id;
    (void)seed_ms;
    return false;
}
void faculty175_face_wifilab_tick(faculty175_face_id_t id, uint32_t now_ms) { (void)id; (void)now_ms; }
void faculty175_face_wifilab_enter(faculty175_face_id_t id) { (void)id; }
void faculty175_face_wifilab_leave(faculty175_face_id_t id) { (void)id; }
void faculty175_face_incidents_draw(uint32_t anim_ms) { websim_placeholder_face("Incidents", anim_ms); }
bool faculty175_face_incidents_action(uint32_t seed_ms) { (void)seed_ms; return false; }
bool faculty175_face_incidents_scroll(int delta) { (void)delta; return false; }
size_t faculty175_face_incidents_scroll_index(void) { return 0; }

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
        case ESP_OK: return "ESP_OK";
        case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_FAIL: return "ESP_FAIL";
        default: return "ESP_ERR";
    }
}

uint32_t esp_random(void)
{
    static uint32_t x = 0x1234abcdU;
    x = x * 1664525u + 1013904223u;
    return x;
}

void *heap_caps_malloc(size_t size, unsigned caps)
{
    (void)caps;
    return malloc(size);
}

void *heap_caps_realloc(void *ptr, size_t size, unsigned caps)
{
    (void)caps;
    return realloc(ptr, size);
}

void heap_caps_free(void *ptr)
{
    free(ptr);
}

size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return 8u * 1024u * 1024u;
}

int64_t esp_timer_get_time(void)
{
    return (int64_t)s_tick_ms * 1000;
}

esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf) { (void)conf; return ESP_FAIL; }
esp_err_t esp_spiffs_info(const char *partition_label, size_t *total, size_t *used)
{
    (void)partition_label;
    if (total) {
        *total = 0;
    }
    if (used) {
        *used = 0;
    }
    return ESP_FAIL;
}
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    (void)type;
    (void)subtype;
    (void)label;
    return NULL;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)s_tick_ms; }

esp_err_t nvs_open(const char *name, int open_mode, nvs_handle_t *out_handle) { (void)name; (void)open_mode; if (out_handle) *out_handle = 1; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value) { (void)handle; (void)key; (void)out_value; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value) { (void)handle; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value) { (void)handle; (void)key; (void)out_value; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) { (void)handle; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length) { (void)handle; (void)key; (void)out_value; if (length) *length = 0; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) { (void)handle; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length) { (void)handle; (void)key; (void)out_value; if (length) *length = 0; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) { (void)handle; (void)key; (void)value; (void)length; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) { (void)handle; (void)key; return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }

bool faculty175_board_audio_ready(void) { return false; }
esp_err_t faculty175_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms) { (void)samples; (void)sample_count; (void)timeout_ms; return ESP_OK; }
esp_err_t faculty175_audio_set_sample_rate(uint32_t hz) { (void)hz; return ESP_OK; }
void faculty175_audio_set_speaker_mute(bool mute) { (void)mute; }
esp_err_t faculty175_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms) { (void)samples; (void)sample_count; (void)timeout_ms; if (out_read) *out_read = 0; return ESP_FAIL; }
esp_err_t faculty175_audio_read_tdm_raw(int16_t *samples, size_t frame_count, size_t *out_frames, uint32_t timeout_ms) { (void)samples; (void)frame_count; (void)timeout_ms; if (out_frames) *out_frames = 0; return ESP_FAIL; }

i2c_master_bus_handle_t faculty175_i2c_bus(void) { return NULL; }
bool faculty175_i2c_probe(uint8_t addr_7bit) { (void)addr_7bit; return false; }
esp_err_t faculty175_i2c_write(uint8_t addr_7bit, const uint8_t *data, size_t len) { (void)addr_7bit; (void)data; (void)len; return ESP_FAIL; }
esp_err_t faculty175_i2c_write_read(uint8_t addr_7bit, const uint8_t *wr, size_t wr_len, uint8_t *rd, size_t rd_len) { (void)addr_7bit; (void)wr; (void)wr_len; (void)rd; (void)rd_len; return ESP_FAIL; }
esp_err_t faculty175_board_init(void) { return ESP_OK; }
bool faculty175_board_pi4ioe_ok(void) { return false; }
int32_t faculty175_board_mic_probe_peak(void) { return 0; }
void faculty175_board_set_backlight(uint8_t percent) { (void)percent; }
void faculty175_board_display_on(bool on) { (void)on; }
bool faculty175_button_pressed(void) { return false; }
bool faculty175_button_just_pressed(void) { return false; }
void faculty175_button_inject_press(void) {}

esp_err_t faculty175_touch_init(void) { return ESP_OK; }
bool faculty175_touch_ready(void) { return false; }
bool faculty175_touch_int_active(void) { return false; }
uint8_t faculty175_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts)
{
    (void)xs;
    (void)ys;
    (void)max_pts;
    return 0;
}
void faculty175_touch_state_update(bool down, int16_t x, int16_t y, uint32_t now_ms)
{
    (void)down;
    (void)x;
    (void)y;
    (void)now_ms;
}
faculty175_touch_state_t faculty175_touch_state_get(void)
{
    return (faculty175_touch_state_t){0};
}

esp_err_t faculty175_wifi_settings_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    if (ssid) {
        snprintf(ssid, ssid_cap, "Astrolabe-Websim");
    }
    if (pass) {
        snprintf(pass, pass_cap, "simulated");
    }
    return ESP_OK;
}
esp_err_t faculty175_wifi_settings_save(const char *ssid, const char *pass) { (void)ssid; (void)pass; return ESP_OK; }
size_t faculty175_wifi_settings_load_known(faculty175_wifi_known_t *out, size_t cap)
{
    if (out != NULL && cap > 0) {
        snprintf(out[0].ssid, sizeof(out[0].ssid), "Astrolabe-Websim");
        snprintf(out[0].pass, sizeof(out[0].pass), "simulated");
        return 1;
    }
    return 0;
}
esp_err_t faculty175_wifi_settings_add_known(const char *ssid, const char *pass, bool make_primary)
{
    (void)ssid;
    (void)pass;
    (void)make_primary;
    return ESP_OK;
}
esp_err_t faculty175_wifi_settings_remove_known(const char *ssid) { (void)ssid; return ESP_OK; }
esp_err_t faculty175_wifi_settings_clear_known(void) { return ESP_OK; }
bool faculty175_wifi_settings_travel_router_enabled(void) { return false; }
esp_err_t faculty175_wifi_settings_set_travel_router_enabled(bool enabled) { (void)enabled; return ESP_OK; }
void faculty175_wifi_settings_set_sta(const char *ssid, const esp_ip4_addr_t *ip) { (void)ssid; (void)ip; }
void faculty175_wifi_settings_set_ap(const char *ssid, const char *pass, const esp_ip4_addr_t *ip) { (void)ssid; (void)pass; (void)ip; }
void faculty175_wifi_settings_set_router_upstream(const char *ssid, const esp_ip4_addr_t *ip) { (void)ssid; (void)ip; }
void faculty175_wifi_settings_set_ap_client_count(unsigned count) { (void)count; }
void faculty175_wifi_settings_clear_runtime(void) {}
void faculty175_wifi_settings_set_scan_suppressed(bool suppressed) { (void)suppressed; }
bool faculty175_wifi_settings_ap_active(void) { return true; }
bool faculty175_wifi_settings_ap_client_connected(void) { return false; }
bool faculty175_wifi_settings_scan_suppressed(void) { return false; }
const char *faculty175_wifi_settings_ssid(void) { return "Astrolabe-Websim"; }
const char *faculty175_wifi_settings_upstream_ssid(void) { return ""; }
const char *faculty175_wifi_settings_url(void) { return "http://localhost:8088"; }
const char *faculty175_wifi_settings_qr_payload(void) { return "WIFI:T:WPA;S:Astrolabe-Websim;P:simulated;;"; }
const char *faculty175_wifi_settings_ap_qr_payload(void) { return "WIFI:T:WPA;S:Astrolabe-Websim;P:simulated;;"; }
const char *faculty175_wifi_settings_page_qr_payload(void) { return "http://localhost:8088/astrolabe-web-sim.html"; }
const char *faculty175_wifi_settings_status(void) { return "websim access point"; }

void faculty175_wifi_monitor_record_connected(const wifi_ap_record_t *ap) { (void)ap; }
void faculty175_wifi_monitor_record_disconnected(const char *ssid, const uint8_t bssid[6], int reason, int rssi)
{
    (void)ssid;
    (void)bssid;
    (void)reason;
    (void)rssi;
}
void faculty175_wifi_monitor_record_ap_client(bool connected, int aid) { (void)connected; (void)aid; }
void faculty175_wifi_monitor_record_scan(uint16_t count) { (void)count; }
void faculty175_wifi_monitor_record_note(const char *type, const char *detail) { (void)type; (void)detail; }
size_t faculty175_wifi_monitor_copy(faculty175_wifi_incident_t *out, size_t cap)
{
    if (out != NULL && cap > 0) {
        memset(out, 0, sizeof(out[0]));
        out[0].seq = 1;
        out[0].uptime_ms = s_tick_ms;
        snprintf(out[0].type, sizeof(out[0].type), "websim");
        snprintf(out[0].ssid, sizeof(out[0].ssid), "Astrolabe-Websim");
        snprintf(out[0].detail, sizeof(out[0].detail), "simulated Wi-Fi event");
        out[0].rssi = -42;
        out[0].channel = 6;
        out[0].authmode = WIFI_AUTH_WPA2_PSK;
        return 1;
    }
    return 0;
}
size_t faculty175_wifi_monitor_count(void) { return 1; }
bool faculty175_wifi_monitor_get_newest(size_t offset, faculty175_wifi_incident_t *out)
{
    if (offset != 0 || out == NULL) {
        return false;
    }
    return faculty175_wifi_monitor_copy(out, 1) == 1;
}
void faculty175_wifi_monitor_clear(void) {}

bool faculty175_rotary_state_get(faculty175_rotary_state_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = (faculty175_rotary_state_t){
        .valid = false,
        .state = 0,
        .rssi_dbm = -127,
        .source = "websim",
        .age_ms = 0,
        .has_style = false,
        .facial_hair = 0,
        .glasses = 0,
        .skin_tone = 0,
        .hair_color = 0,
        .eye_color = 0,
    };
    return false;
}

const char *faculty175_rotary_state_label(uint8_t state)
{
    static const char *const k_labels[FACULTY175_ROTARY_STATE_KIND_COUNT] = {
        "neutral",
        "calm",
        "energized",
        "focused",
        "hopeful",
        "frustrated",
        "anxious",
        "heavy",
    };
    return state < FACULTY175_ROTARY_STATE_KIND_COUNT ? k_labels[state] : "neutral";
}

const char *faculty175_rotary_state_emoji(uint8_t state)
{
    static const char *const k_emojis[FACULTY175_ROTARY_STATE_KIND_COUNT] = {
        ":-|",
        ":-)",
        ":-D",
        ":-!",
        ":-)",
        ">:(",
        ":-/",
        ":'(",
    };
    return state < FACULTY175_ROTARY_STATE_KIND_COUNT ? k_emojis[state] : ":-|";
}

bool faculty175_wifi_lab_is_face(faculty175_face_id_t id)
{
    return id == FACULTY175_FACE_WSCAN || id == FACULTY175_FACE_DEAUTH ||
           id == FACULTY175_FACE_EVILTWIN || id == FACULTY175_FACE_HANDSHAKE;
}
void faculty175_wifi_lab_on_enter(faculty175_face_id_t id) { (void)id; }
void faculty175_wifi_lab_on_leave(faculty175_face_id_t id) { (void)id; }
void faculty175_wifi_lab_tick(faculty175_face_id_t id, uint32_t now_ms) { (void)id; (void)now_ms; }
bool faculty175_wifi_lab_tap(faculty175_face_id_t id) { (void)id; return true; }
bool faculty175_wifi_lab_cycle_target(faculty175_face_id_t id, int delta) { (void)id; (void)delta; return true; }
void faculty175_wifi_lab_get_state(faculty175_wifi_lab_state_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->active = true;
    snprintf(out->status, sizeof(out->status), "websim Wi-Fi lab");
    snprintf(out->detail, sizeof(out->detail), "deterministic demo AP data");
    out->ap_count = 1;
    snprintf(out->aps[0].ssid, sizeof(out->aps[0].ssid), "Astrolabe-Websim");
    out->aps[0].rssi = -42;
    out->aps[0].channel = 6;
    out->aps[0].authmode = WIFI_AUTH_WPA2_PSK;
}
void faculty175_wifi_lab_record_capture(const char *ssid, const char *pass) { (void)ssid; (void)pass; }
esp_err_t faculty175_wifi_lab_scan(void) { return ESP_OK; }
esp_err_t faculty175_wifi_lab_set_target_index(uint8_t index) { (void)index; return ESP_OK; }
esp_err_t faculty175_wifi_lab_export_pcap(void) { return ESP_OK; }

esp_err_t faculty175_quotes_init(void) { return ESP_OK; }
void faculty175_quotes_start_auto_fetch_task(void) {}
void faculty175_quotes_request_refresh(void) {}
bool faculty175_quotes_handle(const char *line) { (void)line; return false; }
bool faculty175_quotes_current(faculty175_quote_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->demo = true;
    snprintf(out->date, sizeof(out->date), "2026-06-05");
    out->index = 1;
    out->total = 1;
    snprintf(out->faculty_slug, sizeof(out->faculty_slug), "hypatia");
    snprintf(out->faculty_name, sizeof(out->faculty_name), "Hypatia");
    snprintf(out->quote, sizeof(out->quote), "Reserve your right to think.");
    snprintf(out->passage, sizeof(out->passage), "websim demo");
    snprintf(out->book_title, sizeof(out->book_title), "Faculty175");
    snprintf(out->book_author, sizeof(out->book_author), "Astrolabe");
    return true;
}
const char *faculty175_quotes_state_name(void) { return "demo"; }
const char *faculty175_quotes_last(void) { return "websim quote"; }

esp_err_t faculty175_rocket_init(void) { return ESP_OK; }
void faculty175_rocket_start_auto_fetch_task(void) {}
void faculty175_rocket_request_refresh(void) {}
bool faculty175_rocket_handle(const char *line) { (void)line; return false; }
bool faculty175_rocket_current(faculty175_rocket_status_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->count = 1;
    out->launches[0].valid = true;
    snprintf(out->launches[0].id, sizeof(out->launches[0].id), "websim");
    snprintf(out->launches[0].name, sizeof(out->launches[0].name), "Astrolabe LVGL Websim");
    snprintf(out->launches[0].vehicle, sizeof(out->launches[0].vehicle), "Faculty175");
    snprintf(out->launches[0].provider, sizeof(out->launches[0].provider), "Castalia");
    snprintf(out->launches[0].pad, sizeof(out->launches[0].pad), "localhost");
    snprintf(out->launches[0].location, sizeof(out->launches[0].location), "Browser");
    snprintf(out->launches[0].weather, sizeof(out->launches[0].weather), "clear");
    snprintf(out->launches[0].info_url, sizeof(out->launches[0].info_url), "http://localhost:8088");
    out->launches[0].net_unix = (int64_t)astrolabe_time_now() + 3600;
    return true;
}
const char *faculty175_rocket_state_name(void) { return "demo"; }
const char *faculty175_rocket_last(void) { return "websim launch"; }
void faculty175_rocket_format_countdown(int64_t launch_unix, char *out, size_t cap)
{
    int64_t delta = launch_unix - (int64_t)astrolabe_time_now();
    if (delta < 0) {
        delta = 0;
    }
    snprintf(out, cap, "T-%02lld:%02lld", (long long)(delta / 3600), (long long)((delta / 60) % 60));
}
void faculty175_rocket_format_local(int64_t launch_unix, char *out, size_t cap)
{
    (void)launch_unix;
    snprintf(out, cap, "today 13:00");
}

void faculty175_tarot_image_request(int idx, const faculty175_tarot_card_t *card) { (void)idx; (void)card; }
bool faculty175_tarot_image_draw_cached(int idx) { (void)idx; return false; }
bool faculty175_tarot_image_busy(void) { return false; }
const char *faculty175_tarot_image_error(void) { return ""; }
bool faculty175_tarot_spiffs_image_get(int idx, const faculty175_tarot_card_t *card, const uint16_t **pixels, int *w, int *h)
{
    (void)idx;
    (void)card;
    if (pixels) {
        *pixels = NULL;
    }
    if (w) {
        *w = 0;
    }
    if (h) {
        *h = 0;
    }
    return false;
}
const char *faculty175_tarot_spiffs_image_error(void) { return ""; }

esp_err_t faculty175_faculty_init(void) { return ESP_OK; }
void faculty175_faculty_set_ui_notify(faculty175_faculty_ui_notify_fn fn) { (void)fn; }
void faculty175_faculty_set_network_fetch_enabled(bool enabled) { (void)enabled; }
void faculty175_faculty_request_bust(const char *slug) { (void)slug; }
void faculty175_faculty_request_bust_download(const char *slug) { (void)slug; }
void faculty175_faculty_prefetch_roster(void) {}
faculty175_faculty_bust_status_t faculty175_faculty_bust_status(void) { return FACULTY175_FACULTY_BUST_IDLE; }
const char *faculty175_faculty_loaded_slug(void) { return ""; }
bool faculty175_faculty_png_decode(const uint8_t *png, size_t png_len, uint16_t *out, uint8_t *opaque, int *out_w, int *out_h)
{
    (void)png;
    (void)png_len;
    (void)out;
    (void)opaque;
    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    return false;
}
bool faculty175_faculty_bust_content_center(int *cx, int *cy)
{
    if (cx) {
        *cx = FACULTY175_FACULTY_BUST_W / 2;
    }
    if (cy) {
        *cy = FACULTY175_FACULTY_BUST_H / 2;
    }
    return false;
}
void faculty175_faculty_bust_blit_origin(int area_x, int area_y, int area_w, int area_h, bool panel_rotated_ccw, int *out_x, int *out_y)
{
    (void)panel_rotated_ccw;
    if (out_x) {
        *out_x = area_x + (area_w - FACULTY175_FACULTY_BUST_W) / 2;
    }
    if (out_y) {
        *out_y = area_y + (area_h - FACULTY175_FACULTY_BUST_H) / 2;
    }
}
bool faculty175_faculty_draw_bust(int x, int y) { (void)x; (void)y; return false; }
bool faculty175_faculty_copy_bust_argb8888(uint8_t *out_bgra, size_t out_cap, int *out_w, int *out_h, char *slug_out, size_t slug_cap)
{
    (void)out_bgra;
    (void)out_cap;
    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    if (slug_out && slug_cap) {
        slug_out[0] = '\0';
    }
    return false;
}

esp_err_t faculty175_apocalypso_init(void) { return ESP_OK; }
void faculty175_apocalypso_start_auto_fetch_task(void) {}
void faculty175_apocalypso_request_refresh(void) {}
bool faculty175_apocalypso_current(faculty175_apocalypso_status_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->demo = true;
    snprintf(out->updated_at, sizeof(out->updated_at), "websim");
    for (int i = 0; i < FACULTY175_APOCALYPSO_AXIS_COUNT; ++i) {
        out->value[i] = 0.25f + 0.05f * (float)(i % 8);
        snprintf(out->quality[i], sizeof(out->quality[i]), "demo");
    }
    return true;
}
const char *faculty175_apocalypso_state_name(void) { return "demo"; }
const char *faculty175_apocalypso_last(void) { return "websim apocalypso"; }

esp_err_t faculty175_ble_init(void) { return ESP_OK; }
bool faculty175_ble_enabled(void) { return true; }
bool faculty175_ble_advertising(void) { return true; }
bool faculty175_ble_scanning(void) { return ((s_tick_ms / 2400u) % 2u) == 0u; }
esp_err_t faculty175_ble_set_enabled(bool enabled) { (void)enabled; return ESP_OK; }
esp_err_t faculty175_ble_set_device_name(const char *name) { (void)name; return ESP_OK; }
const char *faculty175_ble_device_name(void) { return "Astrolabe WebSim"; }
esp_err_t faculty175_ble_scan_start(uint32_t duration_ms) { (void)duration_ms; return ESP_OK; }
void faculty175_ble_radar_tick(uint32_t now_ms) { (void)now_ms; }
size_t faculty175_ble_peers_snapshot(faculty175_ble_peer_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    const char *names[] = {"Astrolabe Camille", "Astrolabe Daniel", "COLMI R10"};
    const int8_t rssi[] = {-58, -72, -66};
    const bool astrolabe[] = {true, true, false};
    const size_t count = cap < 3 ? cap : 3;
    for (size_t i = 0; i < count; ++i) {
        out[i] = (faculty175_ble_peer_t){
            .valid = true,
            .astrolabe = astrolabe[i],
            .known = astrolabe[i],
            .rssi = rssi[i],
            .seen_ms = s_tick_ms,
            .bearing_deg = (uint16_t)((s_tick_ms / 42u + i * 117u) % 360u),
            .range_pct = (uint8_t)(34u + i * 18u),
            .confidence_pct = (uint8_t)(88u - i * 13u),
        };
        snprintf(out[i].name, sizeof(out[i].name), "%s", names[i]);
        for (int b = 0; b < 6; ++b) {
            out[i].addr[b] = (uint8_t)(0x30 + i * 9 + b);
        }
    }
    return count;
}

void faculty175_solar_image_request(bool force) { (void)force; }
bool faculty175_solar_image_draw_cached(void) { return false; }
bool faculty175_solar_image_busy(void) { return false; }
bool faculty175_solar_image_has_cached(void) { return false; }
bool faculty175_solar_image_action(uint32_t seed_ms) { (void)seed_ms; return true; }
const char *faculty175_solar_image_error(void) { return "websim solar"; }
const char *faculty175_solar_image_channel(void) { return "AIA 171"; }
time_t faculty175_solar_image_cached_epoch(void) { return 0; }

esp_err_t faculty175_location_settings_load(faculty175_location_settings_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->lat_deg = 39.7392;
    out->lon_deg = -104.9903;
    out->updated_epoch = (int64_t)astrolabe_time_now();
    snprintf(out->source, sizeof(out->source), "websim");
    return ESP_OK;
}
esp_err_t faculty175_location_settings_save(double lat_deg, double lon_deg, const char *source)
{
    (void)lat_deg;
    (void)lon_deg;
    (void)source;
    return ESP_OK;
}
bool faculty175_location_settings_valid(double lat_deg, double lon_deg)
{
    return lat_deg >= -90.0 && lat_deg <= 90.0 && lon_deg >= -180.0 && lon_deg <= 180.0;
}

unsigned char *stbi_load_from_memory(const unsigned char *buffer,
                                     int len,
                                     int *x,
                                     int *y,
                                     int *channels_in_file,
                                     int desired_channels)
{
    (void)buffer;
    (void)len;
    (void)desired_channels;
    if (x) {
        *x = 0;
    }
    if (y) {
        *y = 0;
    }
    if (channels_in_file) {
        *channels_in_file = 0;
    }
    return NULL;
}
const char *stbi_failure_reason(void) { return "websim image stub"; }
void stbi_image_free(void *retval_from_stbi_load) { free(retval_from_stbi_load); }

esp_err_t faculty175_almanac_init(void) { return ESP_OK; }
void faculty175_almanac_start_auto_fetch_task(void) {}
bool faculty175_almanac_handle(const char *line) { (void)line; return false; }
bool faculty175_almanac_active(void) { return false; }
const char *faculty175_almanac_state_name(void) { return "demo"; }
const char *faculty175_almanac_last(void) { return "websim almanac"; }
const char *faculty175_almanac_manifest_url(void) { return "local demo"; }
bool faculty175_almanac_cached_daily(char *date, size_t date_cap, char *season, size_t season_cap, char *moon, size_t moon_cap, char *prompt, size_t prompt_cap)
{
    snprintf(date, date_cap, "2026-06-05");
    snprintf(season, season_cap, "early summer");
    snprintf(moon, moon_cap, "waxing");
    snprintf(prompt, prompt_cap, "observe the threshold");
    return true;
}
bool faculty175_almanac_cached_daily_ex(char *date, size_t date_cap, char *season, size_t season_cap, char *moon, size_t moon_cap, char *sun, size_t sun_cap, char *event, size_t event_cap, char *planting, size_t planting_cap, char *prompt, size_t prompt_cap)
{
    faculty175_almanac_cached_daily(date, date_cap, season, season_cap, moon, moon_cap, prompt, prompt_cap);
    snprintf(sun, sun_cap, "Gemini");
    snprintf(event, event_cap, "websim sky");
    snprintf(planting, planting_cap, "mint");
    return true;
}
bool faculty175_almanac_cached_phenology(char *date, size_t date_cap, char *subject, size_t subject_cap, char *action, size_t action_cap, char *habitat, size_t habitat_cap, char *prompt, size_t prompt_cap, char *image_path, size_t image_path_cap)
{
    snprintf(date, date_cap, "2026-06-05");
    snprintf(subject, subject_cap, "cottonwood");
    snprintf(action, action_cap, "seed drift");
    snprintf(habitat, habitat_cap, "river edge");
    snprintf(prompt, prompt_cap, "watch the wind");
    snprintf(image_path, image_path_cap, "");
    return true;
}

void faculty175_charts_ensure_family_seed(void) {}
bool faculty175_charts_primary(faculty175_birth_chart_t *out)
{
    if (out) {
        snprintf(out->name, sizeof(out->name), "Ada");
        out->valid = true;
    }
    return true;
}
esp_err_t faculty175_charts_save_primary(const faculty175_birth_chart_t *chart) { (void)chart; return ESP_OK; }
int faculty175_charts_profile_count(void) { return 2; }
bool faculty175_charts_profile_get(int slot, faculty175_birth_chart_t *out) { (void)slot; return faculty175_charts_primary(out); }
esp_err_t faculty175_charts_profile_save(int slot, const faculty175_birth_chart_t *chart) { (void)slot; (void)chart; return ESP_OK; }
int faculty175_charts_active_slot(void) { return 1; }
bool faculty175_charts_set_active_slot(int slot) { (void)slot; return true; }
bool faculty175_charts_cycle_active(int delta, int *slot_out, faculty175_birth_chart_t *profile_out) { (void)delta; if (slot_out) *slot_out = 1; return faculty175_charts_primary(profile_out); }
bool faculty175_charts_active(faculty175_birth_chart_t *out)
{
    bool ok = faculty175_charts_primary(out);
    if (out) {
        snprintf(out->name, sizeof(out->name), "Hypatia");
    }
    return ok;
}
bool faculty175_charts_birth_positions(const faculty175_birth_chart_t *birth, faculty175_chart_positions_t *out)
{
    (void)birth;
    if (!out) {
        return false;
    }
    for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
        out->lon[i] = fmod((double)(s_tick_ms / 100u + i * 47), 360.0);
    }
    out->ok = true;
    return true;
}
bool faculty175_charts_birth_to_utc(const faculty175_birth_chart_t *birth, time_t *utc_out) { (void)birth; if (utc_out) *utc_out = time(NULL); return true; }
const char *faculty175_charts_body_label(int body) { static const char *labels[] = {"Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa"}; return labels[(body >= 0 && body < 7) ? body : 0]; }
const char *faculty175_charts_zodiac_abbr(double lon) { static const char *z[] = {"AR", "TA", "GE", "CN", "LE", "VI", "LI", "SC", "SG", "CP", "AQ", "PI"}; int i = (int)(lon / 30.0); return z[(i >= 0 && i < 12) ? i : 0]; }

#include "faculty175_ephemeris.h"

bool faculty175_ephemeris_fetch_human_design_epoch(time_t utc_epoch, faculty175_hd_positions_t *out)
{
    static const double base[FACULTY175_HD_BODY_COUNT] = {
        280.5, 100.5, 218.3, 296.1, 334.2, 54.7, 72.0, 312.0, 41.0, 350.0, 298.0, 23.0,
    };
    static const double rate[FACULTY175_HD_BODY_COUNT] = {
        0.985647, 0.985647, 13.176358, 4.092334, 1.602130, 0.524021,
        0.083085, 0.033444, 0.011728, 0.005981, 0.003964, -0.052953,
    };
    if (out == NULL) {
        return false;
    }
    const double days = (double)(utc_epoch - 946728000) / 86400.0;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < FACULTY175_HD_BODY_COUNT; ++i) {
        double lon = fmod(base[i] + days * rate[i], 360.0);
        if (lon < 0.0) {
            lon += 360.0;
        }
        out->lon[i] = lon;
    }
    out->lon[FACULTY175_HD_BODY_EARTH] = fmod(out->lon[FACULTY175_HD_BODY_SUN] + 180.0, 360.0);
    out->ok = true;
    out->from_network = false;
    return true;
}

const char *faculty175_ephemeris_hd_body_label(faculty175_hd_body_t body)
{
    static const char *const labels[FACULTY175_HD_BODY_COUNT] = {
        "SUN", "EAR", "MOO", "MER", "VEN", "MAR", "JUP", "SAT", "URA", "NEP", "PLU", "NOD",
    };
    return body >= 0 && body < FACULTY175_HD_BODY_COUNT ? labels[body] : "?";
}
const char *faculty175_charts_role_label(faculty175_chart_role_t role) { (void)role; return "self"; }
bool faculty175_charts_handle(const char *line) { (void)line; return false; }

bool astrolabe_time_valid(void) { return true; }
time_t astrolabe_time_now(void) { return (time_t)(1780629600 + s_tick_ms / 1000u); }
void astrolabe_time_local(struct tm *out)
{
    if (out) {
        time_t now = astrolabe_time_now();
        struct tm *tmp = gmtime(&now);
        if (tmp) {
            *out = *tmp;
        }
    }
}
size_t astrolabe_time_format_local(char *out, size_t cap)
{
    if (out) {
        int n = snprintf(out, cap, "2026-06-05 12:00");
        return n > 0 ? (size_t)n : 0;
    }
    return 0;
}
size_t astrolabe_time_format_utc(char *out, size_t cap)
{
    if (out) {
        int n = snprintf(out, cap, "2026-06-05 18:00");
        return n > 0 ? (size_t)n : 0;
    }
    return 0;
}
