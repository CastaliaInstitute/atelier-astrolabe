#include "paper_board.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "paper_epd.h"
#include "paper_faculty.h"

static const char *TAG = "paper_board";

#define PAPER_AUDIO_I2C_SCL GPIO_NUM_2
#define PAPER_AUDIO_I2C_SDA GPIO_NUM_3
#define PAPER_I2S_MCLK GPIO_NUM_42
#define PAPER_I2S_WS GPIO_NUM_41
#define PAPER_I2S_BCK GPIO_NUM_40
#define PAPER_I2S_DOUT GPIO_NUM_39
#define PAPER_I2S_DIN GPIO_NUM_38
#define PAPER_AUDIO_PWR_EN GPIO_NUM_45
#define PAPER_SPK_EN GPIO_NUM_46
#define PAPER_ES7210_ADDR ES7210_CODEC_DEFAULT_ADDR
#define PAPER_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR
#define PAPER_ES7210_MIC_MASK (ES7210_SEL_MIC1 | ES7210_SEL_MIC2)
#define PAPER_ES7210_CHANNELS 2

#define PAPER_SD_HOST SPI2_HOST
#define PAPER_SD_PIN_CS GPIO_NUM_47
#define PAPER_SD_PIN_SCLK GPIO_NUM_15
#define PAPER_SD_PIN_MOSI GPIO_NUM_13
#define PAPER_SD_PIN_MISO GPIO_NUM_14

#define PAPER_BUTTON_A GPIO_NUM_9
#define PAPER_BUTTON_B GPIO_NUM_10
#define PAPER_BUTTON_C GPIO_NUM_1

#define PAPER_PM1_ADDR 0x6e
#define PAPER_PM1_GPIO_EPD_EN 0
#define PAPER_PM1_GPIO_SD_DEC 1
#define PAPER_PM1_GPIO_SD_PWR_EN 3
#define PAPER_PM1_GPIO_SD_DET_EN 4
#define PAPER_PM1_REG_DEVICE_ID 0x00
#define PAPER_PM1_REG_WAKE_SRC 0x05
#define PAPER_PM1_REG_PWR_CFG 0x06
#define PAPER_PM1_REG_I2C_CFG 0x09
#define PAPER_PM1_REG_WDT_CNT 0x0a
#define PAPER_PM1_REG_GPIO_MODE 0x10
#define PAPER_PM1_REG_GPIO_OUT 0x11
#define PAPER_PM1_REG_GPIO_DRV 0x13
#define PAPER_PM1_REG_GPIO_PUPD0 0x14
#define PAPER_PM1_REG_GPIO_PUPD1 0x15
#define PAPER_PM1_REG_GPIO_FUNC0 0x16
#define PAPER_PM1_REG_GPIO_FUNC1 0x17
#define PAPER_PM1_PWR_CHG_EN BIT0
#define PAPER_PM1_PWR_BOOST_EN BIT3
#define PAPER_PM1_WAKE_SRC_PWRBTN BIT2
#define PAPER_PM1_PULL_NONE 0
#define PAPER_PM1_PULL_UP 1
#define PAPER_PM1_FUNC_GPIO 0

#define PAPER_UI_BG_R 255
#define PAPER_UI_BG_G 255
#define PAPER_UI_BG_B 255
#define PAPER_CONVERSATION_TOP 354
#define PAPER_CONVERSATION_BOTTOM 596
#define PAPER_CONVERSATION_CLEAR_TOP (PAPER_CONVERSATION_TOP - 34)

static i2c_master_bus_handle_t s_audio_i2c;
static i2c_master_dev_handle_t s_pm1_dev;
static esp_codec_dev_handle_t s_spk_codec;
static esp_codec_dev_handle_t s_mic_codec;
static const audio_codec_if_t *s_mic_codec_if;
static const audio_codec_data_if_t *s_i2s_data_if;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static uint16_t *s_fb;
static bool s_audio_ready;
static bool s_sd_ready;
static bool s_pm1_ready;
static uint8_t s_button_prev_mask;
static bool s_button_a_prev;
static bool s_button_b_prev;
static bool s_button_c_prev;
static bool s_display_defer_flush;
static int32_t s_mic_probe_peak;
static uint8_t s_pm1_wake_source;

static uint16_t lcd_pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t paper_ui_bg565(void)
{
    return lcd_pack565(PAPER_UI_BG_R, PAPER_UI_BG_G, PAPER_UI_BG_B);
}

uint16_t paper_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint16_t paper_display_fb_from_logical565(uint16_t logical565)
{
    return logical565;
}

uint16_t paper_display_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint32_t paper_display_bkgd_u32(void)
{
    return ((uint32_t)PAPER_UI_BG_B << 16) | ((uint32_t)PAPER_UI_BG_G << 8) | PAPER_UI_BG_R;
}

static uint8_t rgb565_r(uint16_t c) { return (uint8_t)(((c >> 11) & 0x1F) * 255 / 31); }
static uint8_t rgb565_g(uint16_t c) { return (uint8_t)(((c >> 5) & 0x3F) * 255 / 63); }
static uint8_t rgb565_b(uint16_t c) { return (uint8_t)((c & 0x1F) * 255 / 31); }

static void paper_display_flush(void)
{
    if (s_fb == NULL || s_display_defer_flush) {
        return;
    }
    const esp_err_t err = paper_epd_flush_rgb565(s_fb, PAPER_LCD_W, PAPER_LCD_H);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "display flush failed: %s", esp_err_to_name(err));
    }
}

void paper_display_fill_rgb565(uint16_t color)
{
    if (s_fb == NULL) {
        return;
    }
    for (size_t i = 0; i < (size_t)PAPER_LCD_W * (size_t)PAPER_LCD_H; ++i) {
        s_fb[i] = color;
    }
    paper_display_flush();
}

void paper_display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (s_fb == NULL || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > PAPER_LCD_W) {
        w = PAPER_LCD_W - x;
    }
    if (y + h > PAPER_LCD_H) {
        h = PAPER_LCD_H - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        uint16_t *dst = &s_fb[(y + row) * PAPER_LCD_W + x];
        for (int col = 0; col < w; ++col) {
            dst[col] = color;
        }
    }
    paper_display_flush();
}

void paper_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= PAPER_LCD_H) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int dx = x + col;
            if (dx >= 0 && dx < PAPER_LCD_W) {
                s_fb[dy * PAPER_LCD_W + dx] = pixels[row * w + col];
            }
        }
    }
    paper_display_flush();
}

void paper_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL || opaque == NULL || w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= PAPER_LCD_H) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int dx = x + col;
            const int i = row * w + col;
            if (dx >= 0 && dx < PAPER_LCD_W && opaque[i] != 0) {
                s_fb[dy * PAPER_LCD_W + dx] = pixels[i];
            }
        }
    }
    paper_display_flush();
}

static void draw_bar(int x, int y, int w, int h, uint16_t color)
{
    if (s_fb == NULL || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > PAPER_LCD_W) {
        w = PAPER_LCD_W - x;
    }
    if (y + h > PAPER_LCD_H) {
        h = PAPER_LCD_H - y;
    }
    for (int row = 0; row < h; ++row) {
        uint16_t *dst = &s_fb[(y + row) * PAPER_LCD_W + x];
        for (int col = 0; col < w; ++col) {
            dst[col] = color;
        }
    }
}

static void draw_rect_outline(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 1 || h <= 1) {
        return;
    }
    draw_bar(x, y, w, 1, color);
    draw_bar(x, y + h - 1, w, 1, color);
    draw_bar(x, y, 1, h, color);
    draw_bar(x + w - 1, y, 1, h, color);
}

static void draw_char5x7(char c, int x, int y, uint16_t color, int scale)
{
    static const uint8_t font[59][5] = {
        {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
        {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
        {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
        {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
        {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
        {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
        {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
        {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
        {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
        {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
        {0x14, 0x08, 0x3E, 0x08, 0x14}, /* * */
        {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
        {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
        {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
        {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
        {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
        {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
        {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
        {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
        {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
        {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
        {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
        {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
        {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
        {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
        {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
        {0x08, 0x14, 0x22, 0x41, 0x00}, /* < */
        {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
        {0x00, 0x41, 0x22, 0x14, 0x08}, /* > */
        {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
        {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
        {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
        {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
        {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
        {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
        {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
        {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
        {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
        {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
        {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
        {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
        {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
        {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
        {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
        {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
        {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
        {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
        {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
        {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
        {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    };
    static const uint8_t lower[26][5] = {
        {0x20, 0x54, 0x54, 0x54, 0x78}, /* a */
        {0x7F, 0x48, 0x44, 0x44, 0x38}, /* b */
        {0x38, 0x44, 0x44, 0x44, 0x20}, /* c */
        {0x38, 0x44, 0x44, 0x48, 0x7F}, /* d */
        {0x38, 0x54, 0x54, 0x54, 0x18}, /* e */
        {0x08, 0x7E, 0x09, 0x01, 0x02}, /* f */
        {0x0C, 0x52, 0x52, 0x52, 0x3E}, /* g */
        {0x7F, 0x08, 0x04, 0x04, 0x78}, /* h */
        {0x00, 0x44, 0x7D, 0x40, 0x00}, /* i */
        {0x20, 0x40, 0x44, 0x3D, 0x00}, /* j */
        {0x7F, 0x10, 0x28, 0x44, 0x00}, /* k */
        {0x00, 0x41, 0x7F, 0x40, 0x00}, /* l */
        {0x7C, 0x04, 0x18, 0x04, 0x78}, /* m */
        {0x7C, 0x08, 0x04, 0x04, 0x78}, /* n */
        {0x38, 0x44, 0x44, 0x44, 0x38}, /* o */
        {0x7C, 0x14, 0x14, 0x14, 0x08}, /* p */
        {0x08, 0x14, 0x14, 0x18, 0x7C}, /* q */
        {0x7C, 0x08, 0x04, 0x04, 0x08}, /* r */
        {0x48, 0x54, 0x54, 0x54, 0x20}, /* s */
        {0x04, 0x3F, 0x44, 0x40, 0x20}, /* t */
        {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* u */
        {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* v */
        {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* w */
        {0x44, 0x28, 0x10, 0x28, 0x44}, /* x */
        {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* y */
        {0x44, 0x64, 0x54, 0x4C, 0x44}, /* z */
    };
    const uint8_t *glyph = NULL;
    if (c >= 'a' && c <= 'z') {
        glyph = lower[c - 'a'];
    }
    if (c < 32 || c > 'Z') {
        if (glyph == NULL) {
            c = '?';
        }
    }
    if (glyph == NULL) {
        glyph = font[c - 32];
    }
    for (int col = 0; col < 5; ++col) {
        const uint8_t bits = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if ((bits & (1 << row)) == 0) {
                continue;
            }
            draw_bar(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

static void draw_text5x7(const char *text, int x, int y, uint16_t color, int scale, int max_w)
{
    if (text == NULL) {
        return;
    }
    const int step = 6 * scale;
    int cx = x;
    for (const char *p = text; *p != '\0' && (max_w <= 0 || cx + 5 * scale <= x + max_w); ++p) {
        draw_char5x7(*p, cx, y, color, scale);
        cx += step;
    }
}

static int wrapped_line_count(const char *text, int max_chars, int max_lines)
{
    if (text == NULL || text[0] == '\0' || max_chars <= 0 || max_lines <= 0) {
        return 1;
    }
    int lines = 0;
    const char *p = text;
    while (*p != '\0' && lines < max_lines) {
        while (*p != '\0' && isspace((unsigned char)*p) && *p != '\n') {
            ++p;
        }
        if (*p == '\n') {
            ++p;
            lines++;
            continue;
        }

        int len = 0;
        int last_space = -1;
        const char *start = p;
        while (p[len] != '\0' && p[len] != '\n' && len < max_chars) {
            if (isspace((unsigned char)p[len])) {
                last_space = len;
            }
            len++;
        }
        if (p[len] != '\0' && p[len] != '\n' && last_space > 0) {
            len = last_space;
        }
        if (len <= 0) {
            len = 1;
        }
        p = start + len;
        lines++;
        while (*p != '\0' && isspace((unsigned char)*p) && *p != '\n') {
            ++p;
        }
    }
    return lines > 0 ? lines : 1;
}

static void draw_wrapped_text(const char *text, int x, int y, int max_chars, int max_lines, uint16_t color)
{
    if (text == NULL || max_chars <= 0 || max_lines <= 0) {
        return;
    }
    char line[48];
    int line_no = 0;
    const int line_h = 9;
    const int max_w = max_chars * 6 - 1;
    const char *p = text;

    while (*p != '\0' && line_no < max_lines) {
        while (*p != '\0' && isspace((unsigned char)*p) && *p != '\n') {
            ++p;
        }
        if (*p == '\n') {
            ++p;
            line_no++;
            continue;
        }

        int len = 0;
        int last_space = -1;
        const char *start = p;
        const int cap = max_chars < (int)sizeof(line) - 1 ? max_chars : (int)sizeof(line) - 1;
        while (p[len] != '\0' && p[len] != '\n' && len < cap) {
            if (isspace((unsigned char)p[len])) {
                last_space = len;
            }
            len++;
        }
        if (p[len] != '\0' && p[len] != '\n' && last_space > 0) {
            len = last_space;
        }
        if (len <= 0) {
            len = 1;
        }
        memcpy(line, start, (size_t)len);
        line[len] = '\0';
        draw_text5x7(line, x, y + line_no * line_h, color, 1, max_w);
        p = start + len;
        while (*p != '\0' && isspace((unsigned char)*p) && *p != '\n') {
            ++p;
        }
        line_no++;
    }
}

static void draw_centered_text5x7(const char *text, int y, uint16_t color, int scale)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6 * scale;
    int x = (PAPER_LCD_W - w) / 2;
    if (x < 0) {
        x = 0;
    }
    draw_text5x7(text, x, y, color, scale, PAPER_LCD_W);
}

static int bubble_height_for_text(const char *text, int max_chars)
{
    const int lines = wrapped_line_count(text, max_chars, 6);
    return 12 + lines * 9;
}

static void draw_conversation_bubbles(const paper_memory_message_t *messages, size_t message_count, size_t scroll_offset)
{
    const int top = PAPER_CONVERSATION_TOP;
    const int bottom = PAPER_CONVERSATION_BOTTOM;
    const int bubble_w = 286;
    const int pad = 8;
    const int max_chars = (bubble_w - pad * 2 + 1) / 6;
    const uint16_t ink = lcd_pack565(18, 18, 18);
    const uint16_t user_fill = lcd_pack565(214, 232, 255);
    const uint16_t user_line = lcd_pack565(42, 92, 210);
    const uint16_t faculty_fill = lcd_pack565(255, 224, 222);
    const uint16_t faculty_line = lcd_pack565(210, 58, 62);
    const uint16_t divider = lcd_pack565(220, 220, 220);

    draw_bar(18, top - 8, PAPER_LCD_W - 36, 1, divider);
    if (message_count == 0) {
        draw_centered_text5x7("SPEAK TO START", top + 78, lcd_pack565(100, 100, 100), 2);
        return;
    }

    if (scroll_offset >= message_count) {
        scroll_offset = message_count - 1u;
    }

    const size_t window_end = message_count - 1u - scroll_offset;
    const int available_h = bottom - top;
    size_t window_start = window_end;
    int used_h = 0;
    size_t probe = window_end;
    while (true) {
        const int h = bubble_height_for_text(messages[probe].text, max_chars);
        const int add_h = h + (used_h > 0 ? 6 : 0);
        if (used_h > 0 && used_h + add_h > available_h) {
            break;
        }
        used_h += add_h;
        window_start = probe;
        if (probe == 0) {
            break;
        }
        probe--;
    }

    int y = bottom - used_h;
    if (y < top) {
        y = top;
    }
    for (size_t idx = window_start; idx <= window_end; idx++) {
        const paper_memory_message_t *msg = &messages[idx];
        const int h = bubble_height_for_text(msg->text, max_chars);
        const int x = msg->faculty ? 18 : (PAPER_LCD_W - 18 - bubble_w);
        const uint16_t fill = msg->faculty ? faculty_fill : user_fill;
        const uint16_t line = msg->faculty ? faculty_line : user_line;
        draw_bar(x, y, bubble_w, h - 4, fill);
        draw_rect_outline(x, y, bubble_w, h - 4, line);
        draw_bar(msg->faculty ? x : x + bubble_w - 5, y, 5, h - 4, line);
        draw_wrapped_text(msg->text, x + pad, y + 6, max_chars, 6, ink);

        y += h + 6;
    }

    char position[20];
    snprintf(position, sizeof(position), "SCROLL %u/%u", (unsigned)scroll_offset, (unsigned)message_count);
    draw_text5x7(position, PAPER_LCD_W - 96, top - 28, lcd_pack565(20, 20, 20), 1, 94);
    if (window_start > 0) {
        draw_text5x7("OLD", PAPER_LCD_W - 34, top + 2, ink, 1, 32);
    }
    if (window_end + 1u < message_count) {
        draw_text5x7("NEW", PAPER_LCD_W - 34, bottom - 10, ink, 1, 32);
    }
}

static void clear_conversation_region(void)
{
    draw_bar(0,
             PAPER_CONVERSATION_CLEAR_TOP,
             PAPER_LCD_W,
             PAPER_LCD_H - PAPER_CONVERSATION_CLEAR_TOP,
             paper_ui_bg565());
}

static void draw_status_text(paper_ui_state_t state, const char *faculty_name, const char *detail)
{
    const uint16_t ink = lcd_pack565(20, 20, 20);
    const uint16_t muted = lcd_pack565(95, 95, 95);
    const char *state_text = "LISTEN";
    switch (state) {
        case PAPER_UI_BOOT: state_text = "BOOT"; break;
        case PAPER_UI_WIFI: state_text = "WIFI"; break;
        case PAPER_UI_CAPTURE: state_text = "HEARING"; break;
        case PAPER_UI_THINK: state_text = "CASTALIA"; break;
        case PAPER_UI_SPEAK: state_text = "SPEAK"; break;
        case PAPER_UI_ERROR: state_text = "ERROR"; break;
        case PAPER_UI_LISTEN:
        default: break;
    }
    char header[64];
    snprintf(header, sizeof(header), "%s  %s", state_text, faculty_name != NULL ? faculty_name : "Faculty");
    draw_centered_text5x7(header, 8, ink, 1);
    if (detail != NULL && detail[0] != '\0') {
        draw_centered_text5x7(detail, 24, muted, 1);
    }
}

void paper_display_draw_status(paper_ui_state_t state,
                               const char *faculty_name,
                               const char *detail,
                               uint32_t anim_ms,
                               const uint8_t *waveform,
                               size_t waveform_len,
                               const paper_memory_message_t *messages,
                               size_t message_count,
                               size_t scroll_offset)
{
    (void)anim_ms;
    (void)waveform;
    (void)waveform_len;
    if (s_fb == NULL) {
        return;
    }
    s_display_defer_flush = true;
    for (int y = 0; y < PAPER_LCD_H; ++y) {
        for (int x = 0; x < PAPER_LCD_W; ++x) {
            s_fb[y * PAPER_LCD_W + x] = paper_ui_bg565();
        }
    }

    const int bust_w = 320;
    const int bust_x = (PAPER_LCD_W - bust_w) / 2;
    const int bust_y = 28;
    draw_bar(0, 0, PAPER_LCD_W, PAPER_LCD_H, paper_ui_bg565());
    (void)paper_faculty_draw_bust(bust_x, bust_y);
    draw_status_text(state, faculty_name, detail);
    draw_conversation_bubbles(messages, message_count, scroll_offset);
    s_display_defer_flush = false;
    paper_epd_set_mode(PAPER_EPD_MODE_QUALITY);
    paper_display_flush();
}

void paper_display_draw_conversation(const paper_memory_message_t *messages,
                                     size_t message_count,
                                     size_t scroll_offset)
{
    if (s_fb == NULL) {
        return;
    }
    s_display_defer_flush = true;
    clear_conversation_region();
    draw_conversation_bubbles(messages, message_count, scroll_offset);
    s_display_defer_flush = false;
    paper_epd_set_mode(PAPER_EPD_MODE_FASTEST);
    paper_display_flush();
}

size_t paper_display_bmp_size(void)
{
    return 54u + (size_t)PAPER_LCD_W * (size_t)PAPER_LCD_H * 3u;
}

int paper_display_write_bmp(FILE *out)
{
    if (out == NULL || s_fb == NULL) {
        return -1;
    }
    const uint32_t img = (uint32_t)PAPER_LCD_W * (uint32_t)PAPER_LCD_H * 3u;
    const uint32_t file_size = 54u + img;
    uint8_t hdr[54] = {
        'B', 'M',
        (uint8_t)(file_size), (uint8_t)(file_size >> 8), (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        (uint8_t)(PAPER_LCD_W), (uint8_t)(PAPER_LCD_W >> 8), 0, 0,
        (uint8_t)(PAPER_LCD_H), (uint8_t)(PAPER_LCD_H >> 8), 0, 0,
        1, 0, 24, 0,
    };
    if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr)) {
        return -1;
    }
    int written = (int)sizeof(hdr);
    for (int y = PAPER_LCD_H - 1; y >= 0; --y) {
        for (int x = 0; x < PAPER_LCD_W; ++x) {
            const uint16_t c = s_fb[y * PAPER_LCD_W + x];
            const uint8_t bgr[3] = { rgb565_b(c), rgb565_g(c), rgb565_r(c) };
            if (fwrite(bgr, 1, 3, out) != 3) {
                return -1;
            }
            written += 3;
        }
    }
    return written;
}

static int32_t sample_abs(int16_t s)
{
    return s < 0 ? -(int32_t)s : (int32_t)s;
}

static esp_err_t paper_i2s_set_rate(uint32_t hz)
{
    if (s_i2s_tx == NULL || s_i2s_rx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2s_channel_disable(s_i2s_tx);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "i2s tx dis");
    }
    err = i2s_channel_disable(s_i2s_rx);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "i2s rx dis");
    }
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg), TAG, "i2s tx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_rx, &clk_cfg), TAG, "i2s rx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "i2s rx en");
    return ESP_OK;
}

static esp_err_t paper_i2s_init(uint32_t hz)
{
    if (s_i2s_tx != NULL && s_i2s_rx != NULL) {
        return paper_i2s_set_rate(hz);
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx), TAG, "i2s chan");

    i2s_std_config_t tx_cfg = {};
    tx_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    tx_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    tx_cfg.gpio_cfg.mclk = PAPER_I2S_MCLK;
    tx_cfg.gpio_cfg.bclk = PAPER_I2S_BCK;
    tx_cfg.gpio_cfg.ws = PAPER_I2S_WS;
    tx_cfg.gpio_cfg.dout = PAPER_I2S_DOUT;
    tx_cfg.gpio_cfg.din = GPIO_NUM_NC;

    i2s_std_config_t rx_cfg = tx_cfg;
    rx_cfg.gpio_cfg.dout = GPIO_NUM_NC;
    rx_cfg.gpio_cfg.din = PAPER_I2S_DIN;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &tx_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &rx_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "i2s rx en");

    audio_codec_i2s_cfg_t i2s_cfg = {};
    i2s_cfg.port = I2S_NUM_0;
    i2s_cfg.rx_handle = s_i2s_rx;
    i2s_cfg.tx_handle = s_i2s_tx;
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(s_i2s_data_if != NULL, ESP_ERR_NO_MEM, TAG, "i2s data if");
    return ESP_OK;
}

static esp_err_t paper_i2c_init(void)
{
    if (s_audio_i2c != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = PAPER_AUDIO_I2C_SDA;
    cfg.scl_io_num = PAPER_AUDIO_I2C_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_audio_i2c), TAG, "i2c");
    return ESP_OK;
}

static void paper_i2c_bus_recovery(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << PAPER_AUDIO_I2C_SCL) | (1ULL << PAPER_AUDIO_I2C_SDA);
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    gpio_set_level(PAPER_AUDIO_I2C_SDA, 1);
    gpio_set_level(PAPER_AUDIO_I2C_SCL, 1);
    esp_rom_delay_us(10);
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(PAPER_AUDIO_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(PAPER_AUDIO_I2C_SCL, 1);
        esp_rom_delay_us(5);
        if (gpio_get_level(PAPER_AUDIO_I2C_SDA) == 1) {
            break;
        }
    }
    gpio_set_level(PAPER_AUDIO_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(PAPER_AUDIO_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(PAPER_AUDIO_I2C_SDA, 1);
    esp_rom_delay_us(5);
    gpio_reset_pin(PAPER_AUDIO_I2C_SCL);
    gpio_reset_pin(PAPER_AUDIO_I2C_SDA);
}

static esp_err_t paper_pm1_add_device(void)
{
    if (s_pm1_dev != NULL) {
        return ESP_OK;
    }
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = PAPER_PM1_ADDR;
    dev_cfg.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(s_audio_i2c, &dev_cfg, &s_pm1_dev);
}

static esp_err_t paper_pm1_read_reg(uint8_t reg, uint8_t *value)
{
    if (s_pm1_dev == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_pm1_dev, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static esp_err_t paper_pm1_write_reg(uint8_t reg, uint8_t value)
{
    if (s_pm1_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_pm1_dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t paper_pm1_update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur = 0;
    ESP_RETURN_ON_ERROR(paper_pm1_read_reg(reg, &cur), TAG, "pm1 read");
    cur = (uint8_t)((cur & ~mask) | (value & mask));
    ESP_RETURN_ON_ERROR(paper_pm1_write_reg(reg, cur), TAG, "pm1 write");
    return ESP_OK;
}

static esp_err_t paper_pm1_set_gpio_func(uint8_t pin, uint8_t func)
{
    const uint8_t reg = pin < 4 ? PAPER_PM1_REG_GPIO_FUNC0 : PAPER_PM1_REG_GPIO_FUNC1;
    const uint8_t shift = (uint8_t)((pin < 4 ? pin : pin - 4) * 2);
    return paper_pm1_update_reg(reg, (uint8_t)(0x03u << shift), (uint8_t)(func << shift));
}

static esp_err_t paper_pm1_set_gpio_mode(uint8_t pin, bool output)
{
    return paper_pm1_update_reg(PAPER_PM1_REG_GPIO_MODE,
                                (uint8_t)BIT(pin),
                                output ? (uint8_t)BIT(pin) : 0);
}

static esp_err_t paper_pm1_set_gpio_output(uint8_t pin, bool high)
{
    return paper_pm1_update_reg(PAPER_PM1_REG_GPIO_OUT,
                                (uint8_t)BIT(pin),
                                high ? (uint8_t)BIT(pin) : 0);
}

static esp_err_t paper_pm1_set_gpio_push_pull(uint8_t pin)
{
    return paper_pm1_update_reg(PAPER_PM1_REG_GPIO_DRV, (uint8_t)BIT(pin), 0);
}

static esp_err_t paper_pm1_set_gpio_pull(uint8_t pin, uint8_t pull)
{
    const uint8_t reg = pin < 4 ? PAPER_PM1_REG_GPIO_PUPD0 : PAPER_PM1_REG_GPIO_PUPD1;
    const uint8_t shift = (uint8_t)((pin < 4 ? pin : pin - 4) * 2);
    return paper_pm1_update_reg(reg, (uint8_t)(0x03u << shift), (uint8_t)(pull << shift));
}

static esp_err_t paper_pm1_gpio(uint8_t pin, bool output, bool high, uint8_t pull)
{
    ESP_RETURN_ON_ERROR(paper_pm1_set_gpio_func(pin, PAPER_PM1_FUNC_GPIO), TAG, "pm1 gpio func");
    ESP_RETURN_ON_ERROR(paper_pm1_set_gpio_mode(pin, output), TAG, "pm1 gpio mode");
    if (output) {
        ESP_RETURN_ON_ERROR(paper_pm1_set_gpio_push_pull(pin), TAG, "pm1 gpio drv");
        ESP_RETURN_ON_ERROR(paper_pm1_set_gpio_output(pin, high), TAG, "pm1 gpio out");
    }
    ESP_RETURN_ON_ERROR(paper_pm1_set_gpio_pull(pin, pull), TAG, "pm1 gpio pull");
    return ESP_OK;
}

static esp_err_t paper_pm1_init(void)
{
    ESP_RETURN_ON_ERROR(paper_pm1_add_device(), TAG, "pm1 add");
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(paper_pm1_read_reg(PAPER_PM1_REG_DEVICE_ID, &id), TAG, "pm1 id");
    s_pm1_wake_source = 0;
    esp_err_t wake_err = ESP_FAIL;
    for (int attempt = 0; attempt < 4; ++attempt) {
        wake_err = paper_pm1_read_reg(PAPER_PM1_REG_WAKE_SRC, &s_pm1_wake_source);
        if (wake_err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (wake_err == ESP_OK) {
        ESP_LOGI(TAG, "M5PM1 wake source=0x%02x", s_pm1_wake_source);
        (void)paper_pm1_write_reg(PAPER_PM1_REG_WAKE_SRC, (uint8_t)~s_pm1_wake_source);
    } else {
        ESP_LOGW(TAG, "M5PM1 wake source read failed: %s", esp_err_to_name(wake_err));
    }

    esp_err_t cfg_err = ESP_OK;
    esp_err_t err = paper_pm1_write_reg(PAPER_PM1_REG_I2C_CFG, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 I2C_CFG write failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }
    err = paper_pm1_write_reg(PAPER_PM1_REG_WDT_CNT, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 WDT disable failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }

    err = paper_pm1_gpio(PAPER_PM1_GPIO_SD_DET_EN, true, true, PAPER_PM1_PULL_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 SD_DET_EN config failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }
    err = paper_pm1_gpio(PAPER_PM1_GPIO_SD_DEC, false, false, PAPER_PM1_PULL_UP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 SD_DEC config failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }
    err = paper_pm1_gpio(PAPER_PM1_GPIO_SD_PWR_EN, true, true, PAPER_PM1_PULL_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 SD power config failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }
    err = paper_pm1_gpio(PAPER_PM1_GPIO_EPD_EN, true, true, PAPER_PM1_PULL_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 EPD_EN config failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }
    err = paper_pm1_update_reg(PAPER_PM1_REG_PWR_CFG,
                               PAPER_PM1_PWR_CHG_EN | PAPER_PM1_PWR_BOOST_EN,
                               PAPER_PM1_PWR_CHG_EN | PAPER_PM1_PWR_BOOST_EN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 power config failed: %s", esp_err_to_name(err));
        cfg_err = err;
    }
    if (cfg_err != ESP_OK) {
        ESP_LOGW(TAG, "M5PM1 present but some vendor init writes failed");
    }
    ESP_LOGI(TAG, "M5PM1 ready id=0x%02x", id);
    return ESP_OK;
}

static esp_codec_dev_handle_t paper_spk_codec_init(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = I2C_NUM_0;
    i2c_cfg.addr = PAPER_ES8311_ADDR;
    i2c_cfg.bus_handle = s_audio_i2c;
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PAPER_SPK_EN,
        .use_mclk = true,
    };
    const audio_codec_if_t *codec = es8311_codec_new(&codec_cfg);
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec,
        .data_if = s_i2s_data_if,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    if (dev != NULL) {
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = 2;
        fs.sample_rate = PAPER_AUDIO_RATE;
        (void)esp_codec_dev_open(dev, &fs);
        (void)esp_codec_dev_set_out_vol(dev, 80);
        (void)esp_codec_dev_close(dev);
    }
    return dev;
}

static esp_codec_dev_handle_t paper_mic_codec_init(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = I2C_NUM_0;
    i2c_cfg.addr = PAPER_ES7210_ADDR;
    i2c_cfg.bus_handle = s_audio_i2c;
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es7210_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl,
        .master_mode = false,
        .mic_selected = PAPER_ES7210_MIC_MASK,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    const audio_codec_if_t *codec = es7210_codec_new(&codec_cfg);
    s_mic_codec_if = codec;
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec,
        .data_if = s_i2s_data_if,
    };
    esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
    if (dev != NULL) {
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = PAPER_ES7210_CHANNELS;
        fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1) |
                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3);
        fs.sample_rate = PAPER_AUDIO_RATE;
        fs.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        (void)esp_codec_dev_open(dev, &fs);
        if (s_mic_codec_if != NULL && s_mic_codec_if->set_reg != NULL) {
            (void)s_mic_codec_if->set_reg(s_mic_codec_if, 0x04, 0x01);
            (void)s_mic_codec_if->set_reg(s_mic_codec_if, 0x05, 0x00);
        }
        (void)esp_codec_dev_set_in_gain(dev, 30.0f);
    }
    return dev;
}

static esp_err_t paper_i2s_read_mono(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    if (s_mic_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)timeout_ms;
    int16_t stereo_frame[320 * PAPER_ES7210_CHANNELS];
    if (sample_count > 320) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int ret = esp_codec_dev_read(s_mic_codec, stereo_frame,
                                       (int)(sample_count * PAPER_ES7210_CHANNELS * sizeof(int16_t)));
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        const int16_t left = stereo_frame[i * 2];
        const int16_t right = stereo_frame[i * 2 + 1];
        samples[i] = sample_abs(right) > sample_abs(left) ? right : left;
    }
    if (out_read != NULL) {
        *out_read = sample_count;
    }
    return ESP_OK;
}

static int32_t paper_audio_probe_peak(void)
{
    int16_t frame[320];
    int32_t peak = 0;
    vTaskDelay(pdMS_TO_TICKS(80));
    for (int i = 0; i < 6; ++i) {
        size_t got = 0;
        if (paper_i2s_read_mono(frame, 320, &got, 200) != ESP_OK) {
            continue;
        }
        for (size_t j = 0; j < got; ++j) {
            const int32_t abs = sample_abs(frame[j]);
            if (abs > peak) {
                peak = abs;
            }
        }
    }
    return peak;
}

static esp_err_t paper_sd_mount(void)
{
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = PAPER_SD_PIN_MOSI;
    bus_cfg.miso_io_num = PAPER_SD_PIN_MISO;
    bus_cfg.sclk_io_num = PAPER_SD_PIN_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 8192;
    esp_err_t err = spi_bus_initialize(PAPER_SD_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "sd spi");
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = PAPER_SD_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = PAPER_SD_HOST;
    slot.gpio_cs = PAPER_SD_PIN_CS;
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files = 4;
    mount_cfg.allocation_unit_size = 16 * 1024;
    sdmmc_card_t *card = NULL;
    return esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount_cfg, &card);
}

esp_err_t paper_board_init(void)
{
    if (esp_reset_reason() != ESP_RST_POWERON) {
        paper_i2c_bus_recovery();
    }
    ESP_RETURN_ON_ERROR(paper_i2c_init(), TAG, "i2c");
    s_pm1_ready = (paper_pm1_init() == ESP_OK);
    if (!s_pm1_ready) {
        ESP_LOGW(TAG, "M5PM1 init failed; EPD power/SD detect may be unavailable");
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_err_t epd_err = paper_epd_init();
    if (epd_err != ESP_OK) {
        ESP_LOGW(TAG, "ED2208 init failed: %s", esp_err_to_name(epd_err));
    }

    s_fb = (uint16_t *)heap_caps_malloc((size_t)PAPER_LCD_W * (size_t)PAPER_LCD_H * sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_fb != NULL, ESP_ERR_NO_MEM, TAG, "fb");
    for (size_t i = 0; i < (size_t)PAPER_LCD_W * (size_t)PAPER_LCD_H; ++i) {
        s_fb[i] = paper_ui_bg565();
    }

    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = (1ULL << PAPER_BUTTON_A) | (1ULL << PAPER_BUTTON_B) | (1ULL << PAPER_BUTTON_C);
    btn_cfg.mode = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&btn_cfg), TAG, "buttons");

    gpio_config_t pwr_cfg = {};
    pwr_cfg.pin_bit_mask = (1ULL << PAPER_AUDIO_PWR_EN) | (1ULL << PAPER_SPK_EN);
    pwr_cfg.mode = GPIO_MODE_OUTPUT;
    pwr_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    pwr_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pwr_cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&pwr_cfg), TAG, "audio pwr");
    gpio_set_level(PAPER_AUDIO_PWR_EN, 1);
    gpio_set_level(PAPER_SPK_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    if (paper_i2s_init(PAPER_AUDIO_RATE) == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_reset(s_audio_i2c));
        s_spk_codec = paper_spk_codec_init();
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_reset(s_audio_i2c));
        s_mic_codec = paper_mic_codec_init();
        s_audio_ready = (s_spk_codec != NULL && s_mic_codec != NULL);
        if (s_audio_ready) {
            s_mic_probe_peak = paper_audio_probe_peak();
            if (s_mic_probe_peak <= 0) {
                ESP_LOGE(TAG, "mic probe peak=%ld - ES7210 not capturing", (long)s_mic_probe_peak);
                s_audio_ready = false;
            } else {
                ESP_LOGI(TAG, "mic probe peak=%ld", (long)s_mic_probe_peak);
            }
        }
    }
    if (!s_audio_ready) {
        ESP_LOGW(TAG, "audio init incomplete");
    }

    s_sd_ready = (paper_sd_mount() == ESP_OK);
    if (!s_sd_ready) {
        ESP_LOGW(TAG, "SD mount failed or no card present; pipeline will use SPIFFS fallback");
    }
    if (s_sd_ready) {
        ESP_LOGI(TAG, "SD card mounted at /sdcard");
    }
    return ESP_OK;
}

bool paper_board_audio_ready(void) { return s_audio_ready; }
bool paper_board_pi4ioe_ok(void) { return s_pm1_ready; }
uint8_t paper_board_pm1_wake_source(void) { return s_pm1_wake_source; }
bool paper_board_power_button_wake(void) { return (s_pm1_wake_source & PAPER_PM1_WAKE_SRC_PWRBTN) != 0; }
int32_t paper_board_mic_probe_peak(void) { return s_mic_probe_peak; }
void paper_board_set_backlight(uint8_t percent) { (void)percent; }
bool paper_board_sd_ready(void) { return s_sd_ready; }
const char *paper_capture_mount_path(void) { return s_sd_ready ? "/sdcard" : "/spiffs"; }
const char *paper_capture_file_path(void) { return s_sd_ready ? "/sdcard/facultypaper_utterance.pcm" : "/spiffs/facultypaper_utterance.pcm"; }
const char *paper_capture_partition_label(void) { return s_sd_ready ? NULL : "storage"; }
bool paper_capture_skip_spiffs_mount(void) { return s_sd_ready; }

esp_err_t paper_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    return paper_i2s_read_mono(samples, sample_count, out_read, timeout_ms);
}

esp_err_t paper_audio_raw_probe(int32_t *peak, size_t *samples, size_t *nonzero, uint32_t timeout_ms)
{
    if (s_i2s_rx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int16_t frame[320 * PAPER_ES7210_CHANNELS];
    size_t bytes_read = 0;
    const esp_err_t err = i2s_channel_read(s_i2s_rx, frame, sizeof(frame), &bytes_read, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        return err;
    }
    const size_t count = bytes_read / sizeof(frame[0]);
    int32_t max_abs = 0;
    size_t nz = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t abs = sample_abs(frame[i]);
        if (abs > max_abs) {
            max_abs = abs;
        }
        if (frame[i] != 0) {
            nz++;
        }
    }
    if (peak != NULL) {
        *peak = max_abs;
    }
    if (samples != NULL) {
        *samples = count;
    }
    if (nonzero != NULL) {
        *nonzero = nz;
    }
    return ESP_OK;
}

esp_err_t paper_audio_codec_reg(int reg, int *value)
{
    if (s_mic_codec_if == NULL || s_mic_codec_if->get_reg == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_mic_codec_if->get_reg(s_mic_codec_if, reg, value) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t paper_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    if (s_spk_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int16_t *stereo = (int16_t *)heap_caps_malloc(sample_count * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (stereo == NULL) {
        stereo = (int16_t *)malloc(sample_count * 2 * sizeof(int16_t));
    }
    if (stereo == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        stereo[i * 2] = samples[i];
        stereo[i * 2 + 1] = samples[i];
    }
    (void)timeout_ms;
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = 2;
    fs.sample_rate = PAPER_AUDIO_RATE;
    esp_err_t err = esp_codec_dev_open(s_spk_codec, &fs);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        free(stereo);
        return err;
    }
    (void)esp_codec_dev_set_out_vol(s_spk_codec, 80);
    const int ret = esp_codec_dev_write(s_spk_codec, stereo, (int)(sample_count * 2 * sizeof(int16_t)));
    (void)esp_codec_dev_close(s_spk_codec);
    free(stereo);
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t paper_audio_set_sample_rate(uint32_t hz)
{
    if (hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = paper_i2s_set_rate(hz);
    if (err != ESP_OK) {
        return err;
    }
    if (s_spk_codec != NULL) {
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = 2;
        fs.sample_rate = hz;
        (void)esp_codec_dev_close(s_spk_codec);
        (void)esp_codec_dev_open(s_spk_codec, &fs);
    }
    if (s_mic_codec != NULL) {
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = 1;
        fs.sample_rate = hz;
        (void)esp_codec_dev_close(s_mic_codec);
        (void)esp_codec_dev_open(s_mic_codec, &fs);
    }
    return ESP_OK;
}

void paper_audio_set_speaker_mute(bool mute)
{
    gpio_set_level(PAPER_SPK_EN, mute ? 0 : 1);
    if (s_spk_codec != NULL) {
        (void)esp_codec_dev_set_out_mute(s_spk_codec, mute);
    }
}

bool paper_button_pressed(void)
{
    return gpio_get_level(PAPER_BUTTON_A) == 0 || gpio_get_level(PAPER_BUTTON_B) == 0 ||
           gpio_get_level(PAPER_BUTTON_C) == 0;
}

static uint8_t paper_button_mask(void)
{
    uint8_t mask = 0;
    if (gpio_get_level(PAPER_BUTTON_A) == 0) {
        mask |= BIT0;
    }
    if (gpio_get_level(PAPER_BUTTON_B) == 0) {
        mask |= BIT1;
    }
    if (gpio_get_level(PAPER_BUTTON_C) == 0) {
        mask |= BIT2;
    }
    return mask;
}

static bool paper_gpio_just_pressed(gpio_num_t pin, bool *prev)
{
    const bool now = gpio_get_level(pin) == 0;
    const bool just = now && !*prev;
    *prev = now;
    return just;
}

bool paper_button_just_pressed(void)
{
    const uint8_t now = paper_button_mask();
    const bool just = now != 0 && s_button_prev_mask == 0;
    s_button_prev_mask = now;
    return just;
}

uint8_t paper_button_debug_mask(void) { return paper_button_mask(); }
bool paper_button_a_just_pressed(void) { return paper_gpio_just_pressed(PAPER_BUTTON_A, &s_button_a_prev); }
bool paper_button_b_just_pressed(void) { return paper_gpio_just_pressed(PAPER_BUTTON_B, &s_button_b_prev); }
bool paper_button_c_just_pressed(void) { return paper_gpio_just_pressed(PAPER_BUTTON_C, &s_button_c_prev); }
bool paper_button_up_pressed(void) { return gpio_get_level(PAPER_BUTTON_A) == 0; }
bool paper_button_down_pressed(void) { return gpio_get_level(PAPER_BUTTON_B) == 0; }
bool paper_button_up_just_pressed(void) { return paper_button_a_just_pressed(); }
bool paper_button_down_just_pressed(void) { return paper_button_b_just_pressed(); }
