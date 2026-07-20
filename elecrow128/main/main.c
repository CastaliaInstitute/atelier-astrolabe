#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "noto_emoji_sprites.h"
#include "nvs_flash.h"

static const char *TAG = "elecrow128";

#define ELECROW128_LCD_W 240
#define ELECROW128_LCD_H 240

#define PIN_LCD_SCLK GPIO_NUM_10
#define PIN_LCD_MOSI GPIO_NUM_11
#define PIN_LCD_DC GPIO_NUM_3
#define PIN_LCD_CS GPIO_NUM_9
#define PIN_LCD_RST GPIO_NUM_14
#define PIN_LCD_BACKLIGHT GPIO_NUM_46

#define PIN_TOUCH_SDA GPIO_NUM_6
#define PIN_TOUCH_SCL GPIO_NUM_7
#define PIN_TOUCH_INT GPIO_NUM_5
#define PIN_TOUCH_RST GPIO_NUM_13
#define TOUCH_I2C_PORT I2C_NUM_1
#define TOUCH_I2C_ADDR 0x15

#define PIN_I2C_SDA GPIO_NUM_38
#define PIN_I2C_SCL GPIO_NUM_39

#define PIN_ENCODER_A GPIO_NUM_45
#define PIN_ENCODER_B GPIO_NUM_42
#define PIN_ENCODER_SW GPIO_NUM_41

#define PIN_RGB_LED GPIO_NUM_48
#define PIN_POWER_LIGHT GPIO_NUM_40
#define PIN_POWER_EN_1 GPIO_NUM_1
#define PIN_POWER_EN_2 GPIO_NUM_2

#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define ESPNOW_CHANNEL 6
#define PSYCH_SEND_PERIOD_MS 250
#define PSYCH_LOG_PERIOD_MS 2000
#define PSYCH_DISPLAY_PERIOD_MS 100
#define PSYCH_MAGIC 0x50535943u
#define PSYCH_FACE_COUNT 12
#define LCD_HOST SPI2_HOST

static volatile int s_encoder_steps;
static volatile int s_encoder_edge_accum;
static volatile int s_encoder_raw_edges;
static volatile int s_button_presses;
static uint8_t s_device_mac[6];
static uint32_t s_psych_seq;
static volatile int s_arousal = 50;
static volatile int s_valence = 50;
static volatile int s_focus_axis;
static volatile int s_face_index;
static volatile bool s_psych_changed = true;
static volatile bool s_serial_dump_active;
static esp_lcd_panel_io_handle_t s_lcd_io;
static uint16_t *s_lcd_fb;
static uint16_t *s_lcd_line;
static SemaphoreHandle_t s_lcd_mutex;
static int s_lcd_pending_cmd = -1;
static volatile uint8_t s_encoder_last_ab;
static portMUX_TYPE s_encoder_mux = portMUX_INITIALIZER_UNLOCKED;
static gpio_glitch_filter_handle_t s_encoder_filter_a;
static gpio_glitch_filter_handle_t s_encoder_filter_b;

static void IRAM_ATTR encoder_isr(void *arg);

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t channel;
    uint16_t size;
    uint32_t seq;
    uint32_t uptime_ms;
    uint8_t mac[6];
    int8_t arousal;
    int8_t valence;
    uint8_t focus_axis;
    int16_t encoder_steps;
    uint16_t button_presses;
} psychometer_packet_t;

typedef struct {
    int8_t x;
    int8_t y;
} circumplex_stop_t;

static const int8_t s_quad_delta[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
};

static const circumplex_stop_t s_circumplex_stops[PSYCH_FACE_COUNT] = {
    {100, 0},
    {87, 50},
    {50, 87},
    {0, 100},
    {-50, 87},
    {-87, 50},
    {-100, 0},
    {-87, -50},
    {-50, -87},
    {0, -100},
    {50, -87},
    {87, -50},
};

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static int wrap_face_index(int value)
{
    while (value < 0) {
        value += PSYCH_FACE_COUNT;
    }
    while (value >= PSYCH_FACE_COUNT) {
        value -= PSYCH_FACE_COUNT;
    }
    return value;
}

static void apply_circumplex_stop(int index)
{
    const circumplex_stop_t stop = s_circumplex_stops[wrap_face_index(index)];
    s_valence = clamp_percent(50 + (stop.x * 45) / 100);
    s_arousal = clamp_percent(50 + (stop.y * 45) / 100);
    s_focus_axis = 0;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xf8) << 8) | ((uint16_t)(g & 0xfc) << 3) | ((uint16_t)b >> 3));
}

static void render_yield_if_needed(int row)
{
    if ((row & 0x0f) == 0) {
        vTaskDelay(1);
    }
}

static void lcd_flush_pending_cmd(void)
{
    if (s_lcd_pending_cmd >= 0) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_lcd_io, s_lcd_pending_cmd, NULL, 0));
        s_lcd_pending_cmd = -1;
    }
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_flush_pending_cmd();
    s_lcd_pending_cmd = cmd;
}

static void lcd_data(const void *data, size_t len)
{
    if (len == 0) {
        lcd_flush_pending_cmd();
        return;
    }
    if (s_lcd_pending_cmd >= 0) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_lcd_io, s_lcd_pending_cmd, data, len));
        s_lcd_pending_cmd = -1;
    } else {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_lcd_io, -1, data, len));
    }
}

static void lcd_data_u8(uint8_t data)
{
    lcd_data(&data, 1);
}

static void lcd_data_bytes(const uint8_t *data, size_t len)
{
    lcd_data(data, len);
}

static void lcd_reset(void)
{
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_flush_pending_cmd();
    uint8_t data[4];
    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)x1;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_lcd_io, 0x2a, data, sizeof(data)));
    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)y1;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_lcd_io, 0x2b, data, sizeof(data)));
}

static void lcd_flush(void)
{
    for (int y = 0; y < ELECROW128_LCD_H; ++y) {
        lcd_set_window(0, y, ELECROW128_LCD_W - 1, y);
        memcpy(s_lcd_line, &s_lcd_fb[y * ELECROW128_LCD_W], ELECROW128_LCD_W * sizeof(uint16_t));
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_lcd_io, 0x2c, s_lcd_line, ELECROW128_LCD_W * sizeof(uint16_t)));
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_lcd_io, -1, NULL, 0));
    }
}

static void fb_clear(uint16_t color)
{
    const uint16_t swapped = __builtin_bswap16(color);
    for (int y = 0; y < ELECROW128_LCD_H; ++y) {
        uint16_t *dst = &s_lcd_fb[y * ELECROW128_LCD_W];
        for (int x = 0; x < ELECROW128_LCD_W; ++x) {
            dst[x] = swapped;
        }
        render_yield_if_needed(y);
    }
}

static void fb_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= ELECROW128_LCD_W || y < 0 || y >= ELECROW128_LCD_H) {
        return;
    }
    s_lcd_fb[y * ELECROW128_LCD_W + x] = __builtin_bswap16(color);
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > ELECROW128_LCD_W) {
        w = ELECROW128_LCD_W - x;
    }
    if (y + h > ELECROW128_LCD_H) {
        h = ELECROW128_LCD_H - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    const uint16_t swapped = __builtin_bswap16(color);
    for (int row = 0; row < h; ++row) {
        uint16_t *dst = &s_lcd_fb[(y + row) * ELECROW128_LCD_W + x];
        for (int col = 0; col < w; ++col) {
            dst[col] = swapped;
        }
        render_yield_if_needed(row);
    }
}

static void fb_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
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

static const uint8_t *glyph3x5(char ch)
{
    static const uint8_t digit_glyphs[10][5] = {
        {0x7, 0x5, 0x5, 0x5, 0x7},
        {0x2, 0x6, 0x2, 0x2, 0x7},
        {0x7, 0x1, 0x7, 0x4, 0x7},
        {0x7, 0x1, 0x7, 0x1, 0x7},
        {0x5, 0x5, 0x7, 0x1, 0x1},
        {0x7, 0x4, 0x7, 0x1, 0x7},
        {0x7, 0x4, 0x7, 0x5, 0x7},
        {0x7, 0x1, 0x2, 0x2, 0x2},
        {0x7, 0x5, 0x7, 0x5, 0x7},
        {0x7, 0x5, 0x7, 0x1, 0x7},
    };
    static const uint8_t letter_glyphs[26][5] = {
        {0x2, 0x5, 0x7, 0x5, 0x5}, // A
        {0x6, 0x5, 0x6, 0x5, 0x6}, // B
        {0x3, 0x4, 0x4, 0x4, 0x3}, // C
        {0x6, 0x5, 0x5, 0x5, 0x6}, // D
        {0x7, 0x4, 0x6, 0x4, 0x7}, // E
        {0x7, 0x4, 0x6, 0x4, 0x4}, // F
        {0x3, 0x4, 0x5, 0x5, 0x3}, // G
        {0x5, 0x5, 0x7, 0x5, 0x5}, // H
        {0x7, 0x2, 0x2, 0x2, 0x7}, // I
        {0x1, 0x1, 0x1, 0x5, 0x2}, // J
        {0x5, 0x5, 0x6, 0x5, 0x5}, // K
        {0x4, 0x4, 0x4, 0x4, 0x7}, // L
        {0x5, 0x7, 0x7, 0x5, 0x5}, // M
        {0x5, 0x7, 0x7, 0x7, 0x5}, // N
        {0x2, 0x5, 0x5, 0x5, 0x2}, // O
        {0x6, 0x5, 0x6, 0x4, 0x4}, // P
        {0x2, 0x5, 0x5, 0x7, 0x3}, // Q
        {0x6, 0x5, 0x6, 0x5, 0x5}, // R
        {0x3, 0x4, 0x2, 0x1, 0x6}, // S
        {0x7, 0x2, 0x2, 0x2, 0x2}, // T
        {0x5, 0x5, 0x5, 0x5, 0x7}, // U
        {0x5, 0x5, 0x5, 0x5, 0x2}, // V
        {0x5, 0x5, 0x7, 0x7, 0x5}, // W
        {0x5, 0x5, 0x2, 0x5, 0x5}, // X
        {0x5, 0x5, 0x2, 0x2, 0x2}, // Y
        {0x7, 0x1, 0x2, 0x4, 0x7}, // Z
    };
    static const uint8_t glyph_pct[5] = {0x5, 0x1, 0x2, 0x4, 0x5};
    static const uint8_t glyph_dash[5] = {0x0, 0x0, 0x7, 0x0, 0x0};
    static const uint8_t glyph_space[5] = {0x0, 0x0, 0x0, 0x0, 0x0};

    if (ch >= '0' && ch <= '9') {
        return digit_glyphs[ch - '0'];
    }
    if (ch >= 'A' && ch <= 'Z') {
        return letter_glyphs[ch - 'A'];
    }
    if (ch == '%') {
        return glyph_pct;
    }
    if (ch == '-') {
        return glyph_dash;
    }
    return glyph_space;
}

static void fb_draw_char3x5(int x, int y, char ch, int scale, uint16_t color)
{
    const uint8_t *glyph = glyph3x5(ch);
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if ((glyph[row] & (1 << (2 - col))) != 0) {
                fb_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void fb_draw_text3x5(int x, int y, const char *text, int scale, uint16_t color)
{
    for (int i = 0; text[i] != '\0'; ++i) {
        fb_draw_char3x5(x + i * scale * 4, y, text[i], scale, color);
    }
}

static void fb_draw_arc_text3x5(int cx, int y, const char *text, int scale, uint16_t color)
{
    const int len = (int)strlen(text);
    const int char_step = scale * 4;
    const int total_w = (len > 0) ? (len * char_step - scale) : 0;
    const int start_x = cx - total_w / 2;

    for (int i = 0; i < len; ++i) {
        const int x = start_x + i * char_step;
        const int rel = x + scale - cx;
        const int arc_y = y + (rel * rel) / 300;
        fb_draw_char3x5(x, arc_y, text[i], scale, color);
    }
}

static void fb_fill_circle(int cx, int cy, int r, uint16_t color)
{
    const int rr = r * r;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= rr) {
                fb_pixel(cx + x, cy + y, color);
            }
        }
        render_yield_if_needed(y + r);
    }
}

static void fb_draw_ring(int cx, int cy, int r_outer, int r_inner, uint16_t color)
{
    const int ro = r_outer * r_outer;
    const int ri = r_inner * r_inner;
    for (int y = -r_outer; y <= r_outer; ++y) {
        for (int x = -r_outer; x <= r_outer; ++x) {
            const int d = x * x + y * y;
            if (d <= ro && d >= ri) {
                fb_pixel(cx + x, cy + y, color);
            }
        }
        render_yield_if_needed(y + r_outer);
    }
}

static uint16_t blend_rgb565(uint16_t dst, uint16_t src, uint8_t alpha)
{
    if (alpha == 0) {
        return dst;
    }
    if (alpha == 255) {
        return src;
    }

    const int sr = ((src >> 11) & 0x1f) * 255 / 31;
    const int sg = ((src >> 5) & 0x3f) * 255 / 63;
    const int sb = (src & 0x1f) * 255 / 31;
    const int dr = ((dst >> 11) & 0x1f) * 255 / 31;
    const int dg = ((dst >> 5) & 0x3f) * 255 / 63;
    const int db = (dst & 0x1f) * 255 / 31;
    const int inv = 255 - alpha;

    return rgb565((uint8_t)((sr * alpha + dr * inv + 127) / 255),
                  (uint8_t)((sg * alpha + dg * inv + 127) / 255),
                  (uint8_t)((sb * alpha + db * inv + 127) / 255));
}

static void fb_blit_noto_emoji(int cx, int cy, int index, bool large)
{
    index = wrap_face_index(index);
    const int size = large ? NOTO_EMOJI_LARGE_W : NOTO_EMOJI_SMALL_W;
    const int x0 = cx - size / 2;
    const int y0 = cy - size / 2;

    for (int row = 0; row < size; ++row) {
        const int y = y0 + row;
        if (y < 0 || y >= ELECROW128_LCD_H) {
            continue;
        }
        for (int col = 0; col < size; ++col) {
            const int x = x0 + col;
            if (x < 0 || x >= ELECROW128_LCD_W) {
                continue;
            }
            const int offset = row * size + col;
            const uint16_t src = large ? s_noto_emoji_large_rgb565[index][offset] : s_noto_emoji_small_rgb565[index][offset];
            const uint8_t alpha = large ? s_noto_emoji_large_alpha[index][offset] : s_noto_emoji_small_alpha[index][offset];
            const uint16_t dst = __builtin_bswap16(s_lcd_fb[y * ELECROW128_LCD_W + x]);
            s_lcd_fb[y * ELECROW128_LCD_W + x] = __builtin_bswap16(blend_rgb565(dst, src, alpha));
        }
        render_yield_if_needed(row);
    }
}

static void draw_psychometer(void)
{
    const uint16_t bg = rgb565(7, 10, 16);
    const uint16_t ring = rgb565(86, 104, 136);
    const uint16_t muted = rgb565(34, 45, 62);
    const uint16_t arousal_color = rgb565(78, 210, 176);
    const uint16_t valence_color = rgb565(244, 190, 88);
    const uint16_t focus = rgb565(238, 246, 255);
    const int face_index = wrap_face_index(s_face_index);
    const circumplex_stop_t selected_stop = s_circumplex_stops[face_index];
    const int selected_x = 120 + (selected_stop.x * 90) / 100;
    const int selected_y = 120 - (selected_stop.y * 90) / 100;

    if (s_lcd_mutex != NULL) {
        xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    }
    fb_clear(bg);
    fb_draw_ring(120, 120, 114, 108, ring);
    fb_draw_ring(120, 120, 92, 89, muted);

    fb_fill_rect(30, 120, 180, 2, muted);
    fb_fill_rect(120, 30, 2, 180, muted);
    fb_draw_text3x5(114, 35, "A", 2, arousal_color);
    fb_draw_text3x5(203, 114, "V", 2, valence_color);

    for (int i = 0; i < PSYCH_FACE_COUNT; ++i) {
        const circumplex_stop_t stop = s_circumplex_stops[i];
        const int x = 120 + (stop.x * 90) / 100;
        const int y = 120 - (stop.y * 90) / 100;
        if (i == face_index) {
            fb_fill_circle(x, y, 17, rgb565(238, 246, 255));
            fb_fill_circle(x, y, 14, rgb565(18, 25, 35));
        } else {
            fb_fill_circle(x, y, 13, rgb565(18, 25, 35));
        }
        fb_blit_noto_emoji(x, y, i, false);
    }

    fb_draw_line(120, 120, selected_x, selected_y, focus);
    fb_fill_circle(120, 114, 42, rgb565(18, 25, 35));
    fb_draw_ring(120, 114, 44, 42, focus);
    fb_blit_noto_emoji(120, 112, face_index, true);
    fb_draw_arc_text3x5(120, 151, s_noto_emoji_names[face_index], 2, focus);

    lcd_flush();
    if (s_lcd_mutex != NULL) {
        xSemaphoreGive(s_lcd_mutex);
    }
}

static void print_status(void)
{
    printf("ASTRO_STATUS seq=%" PRIu32 " arousal=%d valence=%d focus=%s enc=%d press=%d mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           s_psych_seq,
           s_arousal,
           s_valence,
           s_focus_axis == 0 ? "arousal" : "valence",
           s_encoder_steps,
           s_button_presses,
           s_device_mac[0],
           s_device_mac[1],
           s_device_mac[2],
           s_device_mac[3],
           s_device_mac[4],
           s_device_mac[5]);
    printf("ASTRO_ENCODER raw_edges=%d pending_edges=%d pins=a%d,b%d,sw%d\n",
           s_encoder_raw_edges,
           s_encoder_edge_accum,
           gpio_get_level(PIN_ENCODER_A),
           gpio_get_level(PIN_ENCODER_B),
           gpio_get_level(PIN_ENCODER_SW));
    fflush(stdout);
}

static void reset_encoder_debug(void)
{
    portENTER_CRITICAL(&s_encoder_mux);
    s_encoder_steps = 0;
    s_encoder_edge_accum = 0;
    s_encoder_raw_edges = 0;
    s_encoder_last_ab = (uint8_t)((gpio_get_level(PIN_ENCODER_A) << 1) | gpio_get_level(PIN_ENCODER_B));
    portEXIT_CRITICAL(&s_encoder_mux);
    s_face_index = 0;
    apply_circumplex_stop(s_face_index);
    s_psych_changed = true;
    printf("ASTRO_ENCODER_ZERO pins=a%d,b%d,sw%d\n",
           gpio_get_level(PIN_ENCODER_A),
           gpio_get_level(PIN_ENCODER_B),
           gpio_get_level(PIN_ENCODER_SW));
    fflush(stdout);
}

static void dump_screenshot_ppm(void)
{
    if (s_lcd_fb == NULL) {
        printf("ASTRO_SHOT_ERROR no_framebuffer\n");
        fflush(stdout);
        return;
    }

    static uint8_t row[ELECROW128_LCD_W * 3];
    s_serial_dump_active = true;
    printf("ASTRO_SHOT_REQUEST\n");
    fflush(stdout);
    if (s_lcd_mutex != NULL) {
        if (xSemaphoreTake(s_lcd_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
            printf("ASTRO_SHOT_ERROR framebuffer_busy\n");
            fflush(stdout);
            s_serial_dump_active = false;
            return;
        }
    }

    printf("ASTRO_SHOT_BEGIN P6 %d %d 255\n", ELECROW128_LCD_W, ELECROW128_LCD_H);
    fflush(stdout);
    for (int y = 0; y < ELECROW128_LCD_H; ++y) {
        for (int x = 0; x < ELECROW128_LCD_W; ++x) {
            const uint16_t pixel = __builtin_bswap16(s_lcd_fb[y * ELECROW128_LCD_W + x]);
            const uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1f);
            const uint8_t g6 = (uint8_t)((pixel >> 5) & 0x3f);
            const uint8_t b5 = (uint8_t)(pixel & 0x1f);
            row[x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            row[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            row[x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
        fwrite(row, 1, sizeof(row), stdout);
        if ((y & 0x0f) == 0) {
            fflush(stdout);
            vTaskDelay(1);
        }
    }
    printf("\nASTRO_SHOT_END bytes=%d\n", ELECROW128_LCD_W * ELECROW128_LCD_H * 3);
    fflush(stdout);

    if (s_lcd_mutex != NULL) {
        xSemaphoreGive(s_lcd_mutex);
    }
    s_serial_dump_active = false;
}

static void serial_command_task(void *ctx)
{
    (void)ctx;
    char line[32] = {};
    size_t len = 0;

    printf("ASTRO_CMD_READY commands=shot,status,help\n");
    fflush(stdout);
    while (true) {
        const int input = getchar();
        if (input == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        const char ch = (char)input;
        if (ch == '\r' || ch == '\n') {
            line[len] = '\0';
            if (strcmp(line, "shot") == 0 || strcmp(line, "screenshot") == 0) {
                dump_screenshot_ppm();
            } else if (strcmp(line, "status") == 0) {
                print_status();
            } else if (strcmp(line, "enczero") == 0) {
                reset_encoder_debug();
            } else if (strcmp(line, "help") == 0 || len == 0) {
                printf("ASTRO_HELP commands: shot, status, enczero, help\n");
                fflush(stdout);
            } else {
                printf("ASTRO_ERROR unknown_command=%s\n", line);
                fflush(stdout);
            }
            len = 0;
            continue;
        }
        if (len + 1 < sizeof(line)) {
            line[len++] = ch;
        } else {
            len = 0;
            printf("ASTRO_ERROR command_too_long\n");
            fflush(stdout);
        }
    }
}

static void log_mac(void)
{
    if (esp_efuse_mac_get_default(s_device_mac) == ESP_OK) {
        ESP_LOGI(TAG,
                 "mac=%02x:%02x:%02x:%02x:%02x:%02x",
                 s_device_mac[0],
                 s_device_mac[1],
                 s_device_mac[2],
                 s_device_mac[3],
                 s_device_mac[4],
                 s_device_mac[5]);
    }
}

static void confirm_ota_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running != NULL && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        ESP_LOGI(TAG, "running partition=%s state=%d", running->label, (int)state);
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "ota mark valid: %s", esp_err_to_name(err));
        }
    }
}

static void init_power_gpio(void)
{
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_POWER_LIGHT) | (1ULL << PIN_POWER_EN_1) | (1ULL << PIN_POWER_EN_2) |
                        (1ULL << PIN_LCD_RST) | (1ULL << PIN_TOUCH_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    gpio_set_level(PIN_POWER_LIGHT, 0);
    gpio_set_level(PIN_POWER_EN_1, 1);
    gpio_set_level(PIN_POWER_EN_2, 1);
    gpio_set_level(PIN_LCD_RST, 1);
    gpio_set_level(PIN_TOUCH_RST, 1);
}

static void init_backlight(uint8_t percent)
{
    ledc_timer_config_t timer = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = PIN_LCD_BACKLIGHT,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = (uint32_t)((percent > 100 ? 100 : percent) * 255u / 100u),
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void init_lcd(void)
{
    s_lcd_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_lcd_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    s_lcd_fb = heap_caps_malloc(ELECROW128_LCD_W * ELECROW128_LCD_H * sizeof(uint16_t),
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lcd_fb == NULL) {
        s_lcd_fb = heap_caps_malloc(ELECROW128_LCD_W * ELECROW128_LCD_H * sizeof(uint16_t), MALLOC_CAP_8BIT);
    }
    ESP_ERROR_CHECK(s_lcd_fb == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    s_lcd_line = heap_caps_malloc(ELECROW128_LCD_W * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(s_lcd_line == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ELECROW128_LCD_W * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.sio_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_lcd_io));

    lcd_reset();

    lcd_cmd(0xef);
    lcd_cmd(0xeb);
    lcd_data_u8(0x14);
    lcd_cmd(0xfe);
    lcd_cmd(0xef);
    lcd_cmd(0xeb);
    lcd_data_u8(0x14);
    lcd_cmd(0x84);
    lcd_data_u8(0x40);
    lcd_cmd(0x85);
    lcd_data_u8(0xff);
    lcd_cmd(0x86);
    lcd_data_u8(0xff);
    lcd_cmd(0x87);
    lcd_data_u8(0xff);
    lcd_cmd(0x88);
    lcd_data_u8(0x0a);
    lcd_cmd(0x89);
    lcd_data_u8(0x21);
    lcd_cmd(0x8a);
    lcd_data_u8(0x00);
    lcd_cmd(0x8b);
    lcd_data_u8(0x80);
    lcd_cmd(0x8c);
    lcd_data_u8(0x01);
    lcd_cmd(0x8d);
    lcd_data_u8(0x01);
    lcd_cmd(0x8e);
    lcd_data_u8(0xff);
    lcd_cmd(0x8f);
    lcd_data_u8(0xff);
    lcd_cmd(0xb6);
    lcd_data_bytes((const uint8_t[]){0x00, 0x00}, 2);
    lcd_cmd(0x36);
    lcd_data_u8(0x08);
    lcd_cmd(0x3a);
    lcd_data_u8(0x05);
    lcd_cmd(0x90);
    lcd_data_bytes((const uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    lcd_cmd(0xbd);
    lcd_data_u8(0x06);
    lcd_cmd(0xbc);
    lcd_data_u8(0x00);
    lcd_cmd(0xff);
    lcd_data_bytes((const uint8_t[]){0x60, 0x01, 0x04}, 3);
    lcd_cmd(0xc3);
    lcd_data_u8(0x13);
    lcd_cmd(0xc4);
    lcd_data_u8(0x13);
    lcd_cmd(0xc9);
    lcd_data_u8(0x22);
    lcd_cmd(0xbe);
    lcd_data_u8(0x11);
    lcd_cmd(0xe1);
    lcd_data_bytes((const uint8_t[]){0x10, 0x0e}, 2);
    lcd_cmd(0xdf);
    lcd_data_bytes((const uint8_t[]){0x21, 0x0c, 0x02}, 3);
    lcd_cmd(0xf0);
    lcd_data_bytes((const uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2a}, 6);
    lcd_cmd(0xf1);
    lcd_data_bytes((const uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6f}, 6);
    lcd_cmd(0xf2);
    lcd_data_bytes((const uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2a}, 6);
    lcd_cmd(0xf3);
    lcd_data_bytes((const uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6f}, 6);
    lcd_cmd(0xed);
    lcd_data_bytes((const uint8_t[]){0x1b, 0x0b}, 2);
    lcd_cmd(0xae);
    lcd_data_u8(0x77);
    lcd_cmd(0xcd);
    lcd_data_u8(0x63);
    lcd_cmd(0x70);
    lcd_data_bytes((const uint8_t[]){0x07, 0x07, 0x04, 0x0e, 0x0f, 0x09, 0x07, 0x08, 0x03}, 9);
    lcd_cmd(0xe8);
    lcd_data_u8(0x34);
    lcd_cmd(0x62);
    lcd_data_bytes((const uint8_t[]){0x18, 0x0d, 0x71, 0xed, 0x70, 0x70, 0x18, 0x0f, 0x71, 0xef, 0x70, 0x70}, 12);
    lcd_cmd(0x63);
    lcd_data_bytes((const uint8_t[]){0x18, 0x11, 0x71, 0xf1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xf3, 0x70, 0x70}, 12);
    lcd_cmd(0x64);
    lcd_data_bytes((const uint8_t[]){0x28, 0x29, 0xf1, 0x01, 0xf1, 0x00, 0x07}, 7);
    lcd_cmd(0x66);
    lcd_data_bytes((const uint8_t[]){0x3c, 0x00, 0xcd, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    lcd_cmd(0x67);
    lcd_data_bytes((const uint8_t[]){0x00, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    lcd_cmd(0x74);
    lcd_data_bytes((const uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4e, 0x00}, 7);
    lcd_cmd(0x98);
    lcd_data_bytes((const uint8_t[]){0x3e, 0x07}, 2);
    lcd_cmd(0x21);
    lcd_cmd(0x11);
    lcd_flush_pending_cmd();
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x29);
    lcd_flush_pending_cmd();
    vTaskDelay(pdMS_TO_TICKS(20));
    draw_psychometer();
}

static esp_err_t init_touch_i2c(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &cfg));
    return i2c_driver_install(TOUCH_I2C_PORT, cfg.mode, 0, 0, 0);
}

static esp_err_t cst816d_read_reg(uint8_t reg, uint8_t *out)
{
    return i2c_master_write_read_device(TOUCH_I2C_PORT, TOUCH_I2C_ADDR, &reg, 1, out, 1, pdMS_TO_TICKS(100));
}

static void probe_touch(void)
{
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    uint8_t gesture = 0;
    esp_err_t err = cst816d_read_reg(0x01, &gesture);
    ESP_LOGI(TAG, "cst816d probe: %s gesture=0x%02x", esp_err_to_name(err), gesture);
}

static void init_encoder_gpio(void)
{
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) | (1ULL << PIN_ENCODER_B) | (1ULL << PIN_ENCODER_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&inputs));
    gpio_set_intr_type(PIN_ENCODER_SW, GPIO_INTR_DISABLE);
    gpio_pin_glitch_filter_config_t filter_a = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = PIN_ENCODER_A,
    };
    gpio_pin_glitch_filter_config_t filter_b = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = PIN_ENCODER_B,
    };
    esp_err_t filter_err = gpio_new_pin_glitch_filter(&filter_a, &s_encoder_filter_a);
    if (filter_err == ESP_OK) {
        ESP_ERROR_CHECK(gpio_glitch_filter_enable(s_encoder_filter_a));
    } else {
        ESP_LOGW(TAG, "encoder A glitch filter unavailable: %s", esp_err_to_name(filter_err));
    }
    filter_err = gpio_new_pin_glitch_filter(&filter_b, &s_encoder_filter_b);
    if (filter_err == ESP_OK) {
        ESP_ERROR_CHECK(gpio_glitch_filter_enable(s_encoder_filter_b));
    } else {
        ESP_LOGW(TAG, "encoder B glitch filter unavailable: %s", esp_err_to_name(filter_err));
    }
    s_encoder_last_ab = (uint8_t)((gpio_get_level(PIN_ENCODER_A) << 1) | gpio_get_level(PIN_ENCODER_B));
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENCODER_A, encoder_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENCODER_B, encoder_isr, NULL));
}

static void IRAM_ATTR encoder_isr(void *arg)
{
    (void)arg;
    const uint8_t ab = (uint8_t)((gpio_get_level(PIN_ENCODER_A) << 1) | gpio_get_level(PIN_ENCODER_B));
    const uint8_t transition = (uint8_t)((s_encoder_last_ab << 2) | ab);
    const int delta = s_quad_delta[transition & 0x0f];
    s_encoder_last_ab = ab;
    if (delta != 0) {
        portENTER_CRITICAL_ISR(&s_encoder_mux);
        s_encoder_edge_accum += delta;
        s_encoder_raw_edges += delta;
        portEXIT_CRITICAL_ISR(&s_encoder_mux);
    }
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_EARLY_LOGW(TAG, "espnow send status=%d", (int)status);
    }
}

static esp_err_t init_espnow(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    const uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast_mac, sizeof(peer.peer_addr));
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err == ESP_ERR_ESPNOW_EXIST) {
        err = ESP_OK;
    }
    ESP_LOGI(TAG, "espnow broadcast channel=%d init=%s", ESPNOW_CHANNEL, esp_err_to_name(err));
    return err;
}

static esp_err_t broadcast_psychometer(uint32_t uptime_ms)
{
    const uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    psychometer_packet_t packet = {
        .magic = PSYCH_MAGIC,
        .version = 1,
        .channel = ESPNOW_CHANNEL,
        .size = sizeof(psychometer_packet_t),
        .seq = ++s_psych_seq,
        .uptime_ms = uptime_ms,
        .arousal = (int8_t)s_arousal,
        .valence = (int8_t)s_valence,
        .focus_axis = (uint8_t)s_focus_axis,
        .encoder_steps = (int16_t)s_encoder_steps,
        .button_presses = (uint16_t)s_button_presses,
    };
    memcpy(packet.mac, s_device_mac, sizeof(packet.mac));
    return esp_now_send(broadcast_mac, (const uint8_t *)&packet, sizeof(packet));
}

static void encoder_task(void *ctx)
{
    (void)ctx;
    int last_sw = gpio_get_level(PIN_ENCODER_SW);
    TickType_t last_send = 0;
    TickType_t last_log = 0;
    TickType_t last_display = 0;

    while (true) {
        int detents = 0;
        portENTER_CRITICAL(&s_encoder_mux);
        while (s_encoder_edge_accum >= 4) {
            s_encoder_edge_accum -= 4;
            ++detents;
        }
        while (s_encoder_edge_accum <= -4) {
            s_encoder_edge_accum += 4;
            --detents;
        }
        portEXIT_CRITICAL(&s_encoder_mux);

        while (detents != 0) {
            const int delta = detents > 0 ? 1 : -1;
            detents -= delta;
            s_encoder_steps += delta;
            s_face_index = wrap_face_index(s_face_index + delta);
            apply_circumplex_stop(s_face_index);
            s_psych_changed = true;
        }

        const int sw = gpio_get_level(PIN_ENCODER_SW);
        if (sw != last_sw && sw == 0) {
            ++s_button_presses;
            s_face_index = 0;
            apply_circumplex_stop(s_face_index);
            s_psych_changed = true;
        }
        last_sw = sw;

        const TickType_t now = xTaskGetTickCount();
        const uint32_t now_ms = (uint32_t)(now * portTICK_PERIOD_MS);
        const bool psych_changed = s_psych_changed;
        if (psych_changed || (now - last_send) >= pdMS_TO_TICKS(PSYCH_SEND_PERIOD_MS)) {
            esp_err_t err = broadcast_psychometer(now_ms);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "espnow broadcast failed: %s", esp_err_to_name(err));
            }
            s_psych_changed = false;
            last_send = now;
        }

        if (!s_serial_dump_active && s_lcd_fb != NULL &&
            (psych_changed || (now - last_display) >= pdMS_TO_TICKS(PSYCH_DISPLAY_PERIOD_MS))) {
            draw_psychometer();
            last_display = now;
        }

        if (!s_serial_dump_active && (now - last_log) > pdMS_TO_TICKS(PSYCH_LOG_PERIOD_MS)) {
            ESP_LOGI(TAG,
                     "psychometer seq=%" PRIu32 " arousal=%d valence=%d focus=%s enc=%d press=%d heap=%" PRIu32
                     " psram=%" PRIu32,
                     s_psych_seq,
                     s_arousal,
                     s_valence,
                     s_focus_axis == 0 ? "arousal" : "valence",
                     s_encoder_steps,
                     s_button_presses,
                     (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            last_log = now;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Astrolabe Elecrow128 bring-up %dx%d", ELECROW128_LCD_W, ELECROW128_LCD_H);
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "app=%s version=%s idf=%s", desc->project_name, desc->version, desc->idf_ver);
    log_mac();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    confirm_ota_boot();

    apply_circumplex_stop(s_face_index);
    init_power_gpio();
    init_backlight(50);
    init_lcd();
    init_encoder_gpio();
    ESP_ERROR_CHECK(init_touch_i2c());
    probe_touch();
    ESP_ERROR_CHECK(init_espnow());

    ESP_LOGI(TAG,
             "vendor pins lcd={sclk:%d mosi:%d dc:%d cs:%d rst:%d bl:%d} touch={sda:%d scl:%d int:%d rst:%d} enc={a:%d b:%d sw:%d}",
             PIN_LCD_SCLK,
             PIN_LCD_MOSI,
             PIN_LCD_DC,
             PIN_LCD_CS,
             PIN_LCD_RST,
             PIN_LCD_BACKLIGHT,
             PIN_TOUCH_SDA,
             PIN_TOUCH_SCL,
             PIN_TOUCH_INT,
             PIN_TOUCH_RST,
             PIN_ENCODER_A,
             PIN_ENCODER_B,
             PIN_ENCODER_SW);

    ESP_LOGI(TAG, "psychometer ready: knob moves around circumplex, press returns to positive valence, ESP-NOW broadcast enabled");

    xTaskCreatePinnedToCore(encoder_task, "elecrow128_psych", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(serial_command_task, "elecrow128_cmd", 4096, NULL, 6, NULL, 0);
}
