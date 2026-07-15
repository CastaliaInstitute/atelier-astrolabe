#include "faculty175_board.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "es7210_adc.h"
#include "audio_codec_ctrl_if.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "astrolabe_round_bezel.h"
#include "faculty175_faculty.h"
#include "faculty175_faculty_roster.h"
#include "faculty175_board_id.h"
#include "faculty175_faces.h"
#include "faculty175_pocketwatch.h"
#include "faculty175_pmu.h"
#include "faculty175_touch.h"
#include "astrolabe_time.h"

static const char *TAG = "faculty_board";
static const int32_t FACULTY175_MIC_MONO_GAIN = 6;

/* Waveshare ESP32-S3-Touch-AMOLED-1.75C — CO5300 466×466 QSPI (NOT 1.8″ SH8601).
 *
 * 1.75C (this target)          vs  faculty18 (1.8″)
 * ─────────────────────────────────────────────────
 * CO5300, 466×466, PCLK=38     SH8601, 368×448, PCLK=11
 * panel gap 6,0                gap 0,0 + 90° flush rotation
 * AXP2101 rails (BLDO1 OLED)   TCA9554 @ I2C 0x20 display power
 * ES7210 + ES8311              ES8311 only
 * LCD RST: GPIO1 (BSP)         LCD RST via TCA9554
 * RGB565 big-endian on wire    SH8601 byte-swapped wire format
 */
#define FACULTY175_LCD_HOST SPI2_HOST
#define FACULTY175_LCD_PIN_CS GPIO_NUM_12
#define FACULTY175_LCD_PIN_PCLK GPIO_NUM_38
#define FACULTY175_LCD_PIN_DATA0 GPIO_NUM_4
#define FACULTY175_LCD_PIN_DATA1 GPIO_NUM_5
#define FACULTY175_LCD_PIN_DATA2 GPIO_NUM_6
#define FACULTY175_LCD_PIN_DATA3 GPIO_NUM_7
#define FACULTY175_LCD_PIN_RST GPIO_NUM_2 /* Arduino pin_config LCD_RESET / shared TP_RST. */

#define FACULTY175_LCD_PANEL_GAP_X 0x06
#define FACULTY175_LCD_PANEL_GAP_Y 0

#define FACULTY175_I2C_PORT I2C_NUM_0
#define FACULTY175_I2S_PORT I2S_NUM_0

#define FACULTY175_AUDIO_I2C_SDA GPIO_NUM_15
#define FACULTY175_AUDIO_I2C_SCL GPIO_NUM_14
#define FACULTY175_I2S_MCLK GPIO_NUM_16
#define FACULTY175_I2S_BCK GPIO_NUM_9
#define FACULTY175_I2S_WS GPIO_NUM_45
#define FACULTY175_I2S_DOUT GPIO_NUM_8
#define FACULTY175_I2S_DIN GPIO_NUM_10
#define FACULTY175_PA_GPIO GPIO_NUM_46
#define FACULTY175_ES7210_ADDR ES7210_CODEC_DEFAULT_ADDR
#define FACULTY175_ES7210_ADDR_7BIT (FACULTY175_ES7210_ADDR >> 1)
#define FACULTY175_ES7210_MODE_CONFIG_REG08 0x08
#define FACULTY175_ES7210_SDP_INTERFACE2_REG12 0x12
#define FACULTY175_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR
#define FACULTY175_ES8311_SYSTEM_REG0D 0x0D
#define FACULTY175_ES8311_SDPIN_REG09 0x09
#define FACULTY175_ES8311_SDPOUT_REG0A 0x0A
#define FACULTY175_ES8311_SYSTEM_REG12 0x12
#define FACULTY175_ES8311_SYSTEM_REG13 0x13
#define FACULTY175_ES8311_DAC_REG31 0x31
#define FACULTY175_ES8311_DAC_REG32 0x32
#define FACULTY175_ES8311_DAC_REG37 0x37
#define FACULTY175_AUDIO_MIN_PROBE_PEAK 1
#define FACULTY175_AUDIO_WARN_PROBE_PEAK 32
#define FACULTY175_SPEAKER_VOLUME 100
#define FACULTY175_AUDIO_MAX_READ_SAMPLES 1024
#define FACULTY175_ES7210_CHANNELS 4
#define FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME 2
#define FACULTY175_ES7210_MIC_A_CH 0
#define FACULTY175_ES7210_MIC_B_CH 2
#define FACULTY175_ES7210_CHANNEL_MASK (ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1) | \
                                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3))
#define FACULTY175_AEC_REF_SAMPLES 65536
#define FACULTY175_I2S_DMA_DESC_NUM 3
#define FACULTY175_I2S_DMA_FRAME_NUM 96
#define FACULTY175_AEC_TAIL_MS 650
#define FACULTY175_AEC_MIN_REF_RMS 80
#define FACULTY175_AEC_MAX_GAIN_Q15 (2 * 32768)
#define FACULTY175_HTTP_SCREEN_QA_SKIP_AUDIO 0
#define FACULTY175_I2C_SCAN_CANDIDATES 0

#define FACULTY175_BUTTON_GPIO GPIO_NUM_0

#define FACULTY175_UI_BG_R 0
#define FACULTY175_UI_BG_G 0
#define FACULTY175_UI_BG_B 0

/** Round panel geometry (466×466 visible circle). */
#define FACULTY175_PANEL_CX (FACULTY175_LCD_W / 2)
#define FACULTY175_PANEL_CY (FACULTY175_LCD_H / 2)
#define FACULTY175_BEZEL_OUTER_R 227
#define FACULTY175_BEZEL_INNER_R 226
#define FACULTY175_NAME_ARC_R 221
#define FACULTY175_TOUCH_TRAIL_LEN 18
#define FACULTY175_TOUCH_TRAIL_MS 720
#define FACULTY175_BEZEL_WAVEFORM_MAX 160

static bool s_audio_ready;
static int32_t s_mic_probe_peak;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_codec_dev_handle_t s_spk_codec;
static esp_codec_dev_handle_t s_mic_codec;
static const audio_codec_data_if_t *s_i2s_data_if;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static SemaphoreHandle_t s_flush_done;
static SemaphoreHandle_t s_display_lock;
static uint16_t *s_fb;
static bool s_button_prev;
static volatile uint32_t s_button_injected_presses;
static uint16_t *s_flush_strip;
static int s_flush_strip_h;
static bool s_speaker_ready;
static int16_t s_audio_read_tdm[FACULTY175_AUDIO_MAX_READ_SAMPLES * FACULTY175_ES7210_CHANNELS *
                                FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME];
static uint32_t s_audio_read_frames;
static int s_audio_selected_ch = -1;
static SemaphoreHandle_t s_audio_read_mux;
static bool s_spk_open;
static bool s_mic_open;
static uint32_t s_spk_rate_hz = FACULTY175_AUDIO_RATE;
static int16_t *s_aec_ref;
static uint32_t s_aec_ref_write;
static uint32_t s_aec_ref_rate_hz = FACULTY175_AUDIO_RATE;
static int64_t s_aec_ref_active_until_us;
static uint32_t s_aec_frames;

static void draw_pixel_safe(int x, int y, uint16_t color);
static portMUX_TYPE s_aec_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_touch_visual_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t t_ms;
} touch_visual_point_t;

static touch_visual_point_t s_touch_trail[FACULTY175_TOUCH_TRAIL_LEN];
static uint8_t s_touch_trail_count;
static bool s_touch_visual_down;
static uint32_t s_touch_visual_last_ms;
static bool s_nav_mode;
static bool s_flush_suspended;
static TaskHandle_t s_boot_watch_task;
static volatile bool s_boot_watch_active;
static volatile bool s_boot_watch_progress_active;
static volatile uint8_t s_boot_watch_step;
static volatile uint8_t s_boot_watch_total = 12;
static char s_boot_watch_detail[32] = "INITIALIZING";
static gpio_num_t s_i2c_sda = FACULTY175_AUDIO_I2C_SDA;
static gpio_num_t s_i2c_scl = FACULTY175_AUDIO_I2C_SCL;
static portMUX_TYPE s_waveform_visual_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_bezel_waveform[FACULTY175_BEZEL_WAVEFORM_MAX];
static uint8_t s_bezel_waveform_stream[FACULTY175_BEZEL_WAVEFORM_MAX];
static size_t s_bezel_waveform_len;
static bool s_bezel_waveform_visible;

static void draw_bezel_nav(void);
static void draw_touch_visual(void);

extern volatile uint32_t g_faculty175_boot_stage;
extern volatile int32_t g_faculty175_boot_last_err;
static void draw_stored_bezel_waveform(void);
static void draw_line_safe(int x0, int y0, int x1, int y1, uint16_t color);
static void draw_bezel_arc(int cx, int cy, int r, float start, float end, uint16_t color);
static void boot_watch_task(void *arg);
esp_err_t faculty175_board_play_boot_chime(void);
static void faculty175_log_i2c_lines(const char *stage);
static void faculty175_log_i2c_gpio_drive_test(const char *stage);
static void faculty175_i2c_gpio_recover(const char *stage);
static void faculty175_i2c_reset_bus(const char *stage);
static const audio_codec_ctrl_if_t *faculty175_codec_ctrl_new(uint8_t addr_7bit);

i2c_master_bus_handle_t faculty175_i2c_bus(void)
{
    return s_i2c_bus;
}

static bool lcd_flush_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    BaseType_t hi = pdFALSE;
    if (s_flush_done != NULL) {
        xSemaphoreGiveFromISR(s_flush_done, &hi);
    }
    return hi == pdTRUE;
}

void faculty175_display_lock(void)
{
    if (s_display_lock != NULL) {
        (void)xSemaphoreTakeRecursive(s_display_lock, portMAX_DELAY);
    }
}

void faculty175_display_unlock(void)
{
    if (s_display_lock != NULL) {
        (void)xSemaphoreGiveRecursive(s_display_lock);
    }
}

#define FACULTY175_LCD_FLUSH_STRIP_H 8

/* CO5300 QSPI (same class as SH8601) wants RGB565 high byte first on the wire. */
static uint16_t rgb565_panel_wire(uint16_t logical565)
{
    return (uint16_t)((logical565 >> 8) | (logical565 << 8));
}

static bool faculty175_flush_strip_alloc(void)
{
    if (s_flush_strip != NULL) {
        return true;
    }
    const int candidates[] = {
        FACULTY175_LCD_FLUSH_STRIP_H,
        16,
        12,
        8,
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const int h = candidates[i];
        const size_t strip_bytes = (size_t)FACULTY175_LCD_W * (size_t)h * sizeof(uint16_t);
        /* Keep the panel strip in internal DMA memory; SPI color queueing rejects some PSRAM-backed buffers. */
        s_flush_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_flush_strip == NULL) {
            s_flush_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA);
        }
        if (s_flush_strip != NULL) {
            s_flush_strip_h = h;
            ESP_LOGI(TAG, "lcd flush strip DMA alloc rows=%d bytes=%u", h, (unsigned)strip_bytes);
            return true;
        }
    }
    ESP_LOGE(TAG, "lcd flush strip DMA alloc failed");
    return false;
}

static void faculty175_display_flush_fb(void)
{
    if (s_panel == NULL || s_fb == NULL || s_flush_done == NULL) {
        return;
    }
    if (!faculty175_flush_strip_alloc()) {
        return;
    }

    const int strip_h = s_flush_strip_h > 0 ? s_flush_strip_h : FACULTY175_LCD_FLUSH_STRIP_H;
    for (int y = 0; y < FACULTY175_LCD_H; y += strip_h) {
        int h = strip_h;
        if (y + h > FACULTY175_LCD_H) {
            h = FACULTY175_LCD_H - y;
        }
        for (int row = 0; row < h; ++row) {
            const uint16_t *src = &s_fb[(y + row) * FACULTY175_LCD_W];
            uint16_t *dst = &s_flush_strip[row * FACULTY175_LCD_W];
            for (int x = 0; x < FACULTY175_LCD_W; ++x) {
                dst[x] = rgb565_panel_wire(src[x]);
            }
        }
        (void)xSemaphoreTake(s_flush_done, 0);
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, FACULTY175_LCD_W, y + h, s_flush_strip) != ESP_OK) {
            ESP_LOGW(TAG, "lcd flush strip y=%d failed", y);
            break;
        }
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(20)) != pdTRUE) {
            ESP_LOGW(TAG, "lcd flush strip y=%d timeout", y);
        }
    }
}

static uint16_t lcd_pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

static uint8_t mix_u8(uint8_t a, uint8_t b, uint8_t amount)
{
    return (uint8_t)(((uint16_t)a * (uint16_t)(255 - amount) + (uint16_t)b * (uint16_t)amount) / 255u);
}

static uint16_t mix_rgb565_rgb888(uint8_t base_r,
                                  uint8_t base_g,
                                  uint8_t base_b,
                                  uint8_t hot_r,
                                  uint8_t hot_g,
                                  uint8_t hot_b,
                                  uint8_t amount)
{
    return rgb565(mix_u8(base_r, hot_r, amount),
                  mix_u8(base_g, hot_g, amount),
                  mix_u8(base_b, hot_b, amount));
}

static uint16_t faculty175_ui_bg565(void)
{
    return rgb565(FACULTY175_UI_BG_R, FACULTY175_UI_BG_G, FACULTY175_UI_BG_B);
}

uint16_t faculty175_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint16_t faculty175_display_fb_from_logical565(uint16_t logical565)
{
    return logical565;
}

uint16_t faculty175_display_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint32_t faculty175_display_bkgd_u32(void)
{
    return ((uint32_t)FACULTY175_UI_BG_B << 16) | ((uint32_t)FACULTY175_UI_BG_G << 8) | FACULTY175_UI_BG_R;
}

size_t faculty175_display_frame_pixel_count(void)
{
    return (size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_H;
}

bool faculty175_display_frame_copy(uint16_t *out, size_t pixel_count)
{
    const size_t needed = faculty175_display_frame_pixel_count();
    if (s_fb == NULL || out == NULL || pixel_count < needed) {
        return false;
    }
    memcpy(out, s_fb, needed * sizeof(uint16_t));
    return true;
}

void faculty175_display_frame_compose_carousel(const uint16_t *from, const uint16_t *to, int shift_px)
{
    if (s_fb == NULL || from == NULL || to == NULL) {
        return;
    }
    if (shift_px > FACULTY175_LCD_W) {
        shift_px = FACULTY175_LCD_W;
    } else if (shift_px < -FACULTY175_LCD_W) {
        shift_px = -FACULTY175_LCD_W;
    }

    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const size_t row = (size_t)y * (size_t)FACULTY175_LCD_W;
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            int src_x = x + shift_px;
            const uint16_t *src = from;
            if (src_x >= FACULTY175_LCD_W) {
                src = to;
                src_x -= FACULTY175_LCD_W;
            } else if (src_x < 0) {
                src = to;
                src_x += FACULTY175_LCD_W;
            }
            s_fb[row + (size_t)x] = src[row + (size_t)src_x];
        }
    }
}

void faculty175_display_frame_compose_vertical(const uint16_t *from, const uint16_t *to, int shift_px)
{
    if (s_fb == NULL || from == NULL || to == NULL) {
        return;
    }
    if (shift_px > FACULTY175_LCD_H) {
        shift_px = FACULTY175_LCD_H;
    } else if (shift_px < -FACULTY175_LCD_H) {
        shift_px = -FACULTY175_LCD_H;
    }

    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        int src_y = y + shift_px;
        const uint16_t *src = from;
        if (src_y >= FACULTY175_LCD_H) {
            src = to;
            src_y -= FACULTY175_LCD_H;
        } else if (src_y < 0) {
            src = to;
            src_y += FACULTY175_LCD_H;
        }
        const size_t dst_row = (size_t)y * (size_t)FACULTY175_LCD_W;
        const size_t src_row = (size_t)src_y * (size_t)FACULTY175_LCD_W;
        memcpy(&s_fb[dst_row], &src[src_row], (size_t)FACULTY175_LCD_W * sizeof(uint16_t));
    }
}

void faculty175_display_frame_compose_radial(const uint16_t *from, const uint16_t *to, int radius_px)
{
    if (s_fb == NULL || from == NULL || to == NULL) {
        return;
    }
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const int max_r = FACULTY175_LCD_W > FACULTY175_LCD_H ? FACULTY175_LCD_W : FACULTY175_LCD_H;
    if (radius_px < 0) {
        radius_px = 0;
    } else if (radius_px > max_r) {
        radius_px = max_r;
    }
    const int32_t r2 = (int32_t)radius_px * (int32_t)radius_px;
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const int32_t dy = (int32_t)y - (int32_t)cy;
        const size_t row = (size_t)y * (size_t)FACULTY175_LCD_W;
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const int32_t dx = (int32_t)x - (int32_t)cx;
            const size_t idx = row + (size_t)x;
            s_fb[idx] = (dx * dx + dy * dy <= r2) ? to[idx] : from[idx];
        }
    }
}

void faculty175_display_frame_compose_nav_preview(const uint16_t *center,
                                                  const uint16_t *left,
                                                  const uint16_t *right,
                                                  const uint16_t *up,
                                                  const uint16_t *down)
{
    if (s_fb == NULL || center == NULL) {
        return;
    }

    const int inset_x = (FACULTY175_LCD_W * 5) / 100;
    const int inset_y = (FACULTY175_LCD_H * 5) / 100;
    const int center_w = FACULTY175_LCD_W - (inset_x * 2);
    const int center_h = FACULTY175_LCD_H - (inset_y * 2);
    const int w = FACULTY175_LCD_W;
    const int h = FACULTY175_LCD_H;
    const uint16_t bg = faculty175_ui_bg565();

    for (int y = 0; y < h; ++y) {
        const size_t row = (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
            const uint16_t *src = NULL;
            int sx = x;
            int sy = y;

            if (x >= inset_x && x < inset_x + center_w && y >= inset_y && y < inset_y + center_h) {
                src = center;
                sx = ((x - inset_x) * w) / center_w;
                sy = ((y - inset_y) * h) / center_h;
            } else if (x < inset_x && left != NULL) {
                src = left;
                sx = w - inset_x + x;
            } else if (x >= inset_x + center_w && right != NULL) {
                src = right;
                sx = x - (inset_x + center_w);
            } else if (y < inset_y && up != NULL) {
                src = up;
                sy = h - inset_y + y;
            } else if (y >= inset_y + center_h && down != NULL) {
                src = down;
                sy = y - (inset_y + center_h);
            }

            s_fb[row + (size_t)x] = src != NULL ? src[(size_t)sy * (size_t)w + (size_t)sx] : bg;
        }
    }

    const uint16_t edge = rgb565(92, 232, 255);
    for (int x = inset_x; x < inset_x + center_w; ++x) {
        draw_pixel_safe(x, inset_y, edge);
        draw_pixel_safe(x, inset_y + center_h - 1, edge);
    }
    for (int y = inset_y; y < inset_y + center_h; ++y) {
        draw_pixel_safe(inset_x, y, edge);
        draw_pixel_safe(inset_x + center_w - 1, y, edge);
    }
}

static esp_err_t faculty175_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }
    faculty175_i2c_gpio_recover("i2c-gpio-recover");
    faculty175_log_i2c_lines("i2c-pre-recover");
    typedef struct {
        gpio_num_t sda;
        gpio_num_t scl;
        const char *label;
    } faculty175_i2c_candidate_t;
    static const faculty175_i2c_candidate_t candidates[] = {
        {GPIO_NUM_15, GPIO_NUM_14, "175-default"},
        {GPIO_NUM_11, GPIO_NUM_10, "hybrid-11/10"},
        {GPIO_NUM_42, GPIO_NUM_41, "alt-42/41"},
    };

    int best_score = -1;
    size_t best_idx = 0;
#if FACULTY175_I2C_SCAN_CANDIDATES
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        i2c_master_bus_handle_t try_bus = NULL;
        const i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = candidates[i].sda,
            .scl_io_num = candidates[i].scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {
                .enable_internal_pullup = true,
            },
        };
        if (i2c_new_master_bus(&cfg, &try_bus) != ESP_OK || try_bus == NULL) {
            continue;
        }

        int score = 0;
        if (i2c_master_probe(try_bus, 0x18, 20) == ESP_OK) {
            score += 8;
        }
        if (i2c_master_probe(try_bus, 0x40, 20) == ESP_OK) {
            score += 8;
        }
        if (i2c_master_probe(try_bus, 0x34, 20) == ESP_OK) {
            score += 4;
        }
        if (i2c_master_probe(try_bus, 0x5a, 20) == ESP_OK || i2c_master_probe(try_bus, 0x15, 20) == ESP_OK) {
            score += 2;
        }
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
        (void)i2c_del_master_bus(try_bus);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#else
    best_score = 0;
#endif

    s_i2c_sda = candidates[best_idx].sda;
    s_i2c_scl = candidates[best_idx].scl;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = FACULTY175_I2C_PORT,
        .sda_io_num = s_i2c_sda,
        .scl_io_num = s_i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c_bus), TAG, "i2c bus");
    ESP_LOGI(TAG,
             "i2c-selected %s sda=%d scl=%d score=%d",
             candidates[best_idx].label,
             (int)s_i2c_sda,
             (int)s_i2c_scl,
             best_score);
    vTaskDelay(pdMS_TO_TICKS(50));
    faculty175_log_i2c_lines("i2c-ready");
    return ESP_OK;
}

bool faculty175_i2c_probe(uint8_t addr_7bit)
{
    if (faculty175_i2c_init() != ESP_OK || s_i2c_bus == NULL) {
        return false;
    }
    return i2c_master_probe(s_i2c_bus, addr_7bit, 20) == ESP_OK;
}

static esp_err_t faculty175_i2c_add_device(uint8_t addr_7bit, uint32_t hz, i2c_master_dev_handle_t *out_dev)
{
    if (out_dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(faculty175_i2c_init(), TAG, "i2c init");
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr_7bit,
        .scl_speed_hz = hz,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, out_dev);
}

static esp_err_t faculty175_i2c_hw_write(uint8_t addr_7bit, const uint8_t *data, size_t len)
{
    if (data == NULL && len != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(faculty175_i2c_add_device(addr_7bit, 100000, &dev), TAG, "i2c add");
    const esp_err_t err = i2c_master_transmit(dev, data, len, 100);
    (void)i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t faculty175_i2c_write(uint8_t addr_7bit, const uint8_t *data, size_t len)
{
    return faculty175_i2c_hw_write(addr_7bit, data, len);
}

static esp_err_t faculty175_i2c_hw_write_read(uint8_t addr_7bit,
                                              const uint8_t *wr,
                                              size_t wr_len,
                                              uint8_t *rd,
                                              size_t rd_len)
{
    if ((wr == NULL && wr_len != 0) || (rd == NULL && rd_len != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(faculty175_i2c_add_device(addr_7bit, 100000, &dev), TAG, "i2c add");
    const esp_err_t err = i2c_master_transmit_receive(dev, wr, wr_len, rd, rd_len, 100);
    (void)i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t faculty175_i2c_write_read(uint8_t addr_7bit,
                                    const uint8_t *wr,
                                    size_t wr_len,
                                    uint8_t *rd,
                                    size_t rd_len)
{
    return faculty175_i2c_hw_write_read(addr_7bit, wr, wr_len, rd, rd_len);
}

static void faculty175_log_i2c_lines(const char *stage)
{
    ESP_LOGI(TAG,
             "%s SDA%d=%d SCL%d=%d TP_RST2=%d TP_INT11=%d",
             stage != NULL ? stage : "i2c-lines",
             (int)s_i2c_sda,
             gpio_get_level(s_i2c_sda),
             (int)s_i2c_scl,
             gpio_get_level(s_i2c_scl),
             gpio_get_level(GPIO_NUM_2),
             gpio_get_level(GPIO_NUM_11));
}

static void faculty175_log_i2c_gpio_drive_test(const char *stage)
{
    const gpio_config_t out = {
        .pin_bit_mask = (1ULL << FACULTY175_AUDIO_I2C_SDA) | (1ULL << FACULTY175_AUDIO_I2C_SCL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&out) != ESP_OK) {
        return;
    }
    gpio_set_level(FACULTY175_AUDIO_I2C_SDA, 1);
    gpio_set_level(FACULTY175_AUDIO_I2C_SCL, 1);
    esp_rom_delay_us(100);
    ESP_LOGI(TAG,
             "%s drive-high SDA%d=%d SCL%d=%d",
             stage != NULL ? stage : "i2c-drive",
             (int)FACULTY175_AUDIO_I2C_SDA,
             gpio_get_level(FACULTY175_AUDIO_I2C_SDA),
             (int)FACULTY175_AUDIO_I2C_SCL,
             gpio_get_level(FACULTY175_AUDIO_I2C_SCL));
}

static void faculty175_i2c_gpio_recover(const char *stage)
{
    const gpio_config_t od = {
        .pin_bit_mask = (1ULL << s_i2c_sda) | (1ULL << s_i2c_scl),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&od) != ESP_OK) {
        return;
    }

    gpio_set_level(s_i2c_sda, 1);
    gpio_set_level(s_i2c_scl, 1);
    esp_rom_delay_us(20);

    for (int i = 0; i < 18 && (!gpio_get_level(s_i2c_sda) || !gpio_get_level(s_i2c_scl)); ++i) {
        gpio_set_level(s_i2c_scl, 0);
        esp_rom_delay_us(8);
        gpio_set_level(s_i2c_scl, 1);
        esp_rom_delay_us(8);
    }

    gpio_set_level(s_i2c_sda, 0);
    esp_rom_delay_us(8);
    gpio_set_level(s_i2c_scl, 1);
    esp_rom_delay_us(8);
    gpio_set_level(s_i2c_sda, 1);
    esp_rom_delay_us(20);

    const gpio_config_t idle = {
        .pin_bit_mask = (1ULL << s_i2c_sda) | (1ULL << s_i2c_scl),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&idle);
    vTaskDelay(pdMS_TO_TICKS(2));
    faculty175_log_i2c_lines(stage != NULL ? stage : "i2c-recovered");
}

static void faculty175_i2c_reset_bus(const char *stage)
{
    if (s_i2c_bus != NULL) {
        (void)i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    faculty175_i2c_gpio_recover(stage);
}

static int32_t sample_abs(int16_t s)
{
    return s < 0 ? -(int32_t)s : (int32_t)s;
}

static int16_t audio_sample_host_order(int16_t sample)
{
    return sample;
}

static uint16_t codec_channel_mask(int ch)
{
    return ESP_CODEC_DEV_MAKE_CHANNEL_MASK(ch);
}

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    uint64_t result = 0;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static void faculty175_aec_reset(uint32_t rate_hz)
{
    portENTER_CRITICAL(&s_aec_mux);
    if (s_aec_ref != NULL) {
        memset(s_aec_ref, 0, FACULTY175_AEC_REF_SAMPLES * sizeof(s_aec_ref[0]));
    }
    s_aec_ref_write = 0;
    s_aec_ref_rate_hz = rate_hz == 0 ? FACULTY175_AUDIO_RATE : rate_hz;
    s_aec_ref_active_until_us = 0;
    portEXIT_CRITICAL(&s_aec_mux);
}

static void faculty175_aec_push_reference(const int16_t *samples, size_t sample_count)
{
    if (s_aec_ref == NULL || samples == NULL || sample_count == 0) {
        return;
    }
    const int channels = s_spk_open ? 2 : 1;
    const size_t frames = channels == 2 ? sample_count / 2u : sample_count;
    if (frames == 0) {
        return;
    }

    portENTER_CRITICAL(&s_aec_mux);
    for (size_t i = 0; i < frames; ++i) {
        int32_t mono = samples[i * (size_t)channels];
        if (channels == 2) {
            mono = (mono + samples[i * 2u + 1u]) / 2;
        }
        s_aec_ref[s_aec_ref_write & (FACULTY175_AEC_REF_SAMPLES - 1u)] = (int16_t)mono;
        s_aec_ref_write++;
    }
    s_aec_ref_rate_hz = s_spk_rate_hz == 0 ? FACULTY175_AUDIO_RATE : s_spk_rate_hz;
    s_aec_ref_active_until_us = esp_timer_get_time() + (int64_t)FACULTY175_AEC_TAIL_MS * 1000;
    portEXIT_CRITICAL(&s_aec_mux);
}

static int16_t faculty175_aec_ref_at(uint32_t newest_write, uint32_t delay_samples, size_t i)
{
    const uint32_t idx = newest_write - delay_samples + (uint32_t)i;
    return s_aec_ref[idx & (FACULTY175_AEC_REF_SAMPLES - 1u)];
}

static void faculty175_aec_process(int16_t *samples, size_t sample_count)
{
    if (s_aec_ref == NULL || samples == NULL || sample_count == 0 ||
        esp_timer_get_time() > s_aec_ref_active_until_us) {
        return;
    }

    const uint32_t rate_hz = s_aec_ref_rate_hz == 0 ? FACULTY175_AUDIO_RATE : s_aec_ref_rate_hz;
    const uint32_t write_pos = s_aec_ref_write;
    const uint32_t delays_ms[] = {24, 36, 48, 64, 80, 104, 128};
    int64_t best_corr = 0;
    uint64_t best_ref_energy = 0;
    uint32_t best_delay = 0;

    portENTER_CRITICAL(&s_aec_mux);
    for (size_t d = 0; d < sizeof(delays_ms) / sizeof(delays_ms[0]); ++d) {
        const uint32_t delay = (uint32_t)(((uint64_t)rate_hz * delays_ms[d]) / 1000u);
        if (delay + sample_count + 2u >= FACULTY175_AEC_REF_SAMPLES) {
            continue;
        }
        int64_t corr = 0;
        uint64_t ref_energy = 0;
        for (size_t i = 0; i < sample_count; ++i) {
            const int32_t ref = faculty175_aec_ref_at(write_pos, delay, i);
            corr += (int64_t)samples[i] * ref;
            ref_energy += (uint64_t)(ref * ref);
        }
        const uint64_t corr_abs = (uint64_t)(corr < 0 ? -corr : corr);
        const uint64_t best_abs = (uint64_t)(best_corr < 0 ? -best_corr : best_corr);
        if (ref_energy > 0 && corr_abs * (best_ref_energy + 1u) > best_abs * (ref_energy + 1u)) {
            best_corr = corr;
            best_ref_energy = ref_energy;
            best_delay = delay;
        }
    }

    const uint32_t ref_rms = best_ref_energy == 0 ? 0 : isqrt_u64(best_ref_energy / sample_count);
    if (best_delay == 0 || ref_rms < FACULTY175_AEC_MIN_REF_RMS) {
        portEXIT_CRITICAL(&s_aec_mux);
        return;
    }

    int64_t gain_q15 = (best_corr * 32768) / (int64_t)(best_ref_energy + 1u);
    if (gain_q15 > FACULTY175_AEC_MAX_GAIN_Q15) {
        gain_q15 = FACULTY175_AEC_MAX_GAIN_Q15;
    } else if (gain_q15 < -FACULTY175_AEC_MAX_GAIN_Q15) {
        gain_q15 = -FACULTY175_AEC_MAX_GAIN_Q15;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        const int32_t ref = faculty175_aec_ref_at(write_pos, best_delay, i);
        int32_t sample = (int32_t)samples[i] - (int32_t)((gain_q15 * ref) / 32768);
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }
        samples[i] = (int16_t)sample;
    }
    portEXIT_CRITICAL(&s_aec_mux);

    if ((++s_aec_frames % 50u) == 0u) {
        ESP_LOGI(TAG, "aec delay=%ums gain=%.2f ref_rms=%u",
                 (unsigned)((best_delay * 1000u) / rate_hz),
                 (double)gain_q15 / 32768.0,
                 (unsigned)ref_rms);
    }
}

static esp_err_t faculty175_i2s_set_rate(uint32_t hz)
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
    i2s_tdm_clk_config_t tdm_clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg), TAG, "i2s tx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_tdm_clock(s_i2s_rx, &tdm_clk_cfg), TAG, "i2s rx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "i2s rx en");
    return ESP_OK;
}

static esp_err_t faculty175_i2s_init(uint32_t hz)
{
    if (s_i2s_tx != NULL && s_i2s_rx != NULL) {
        return faculty175_i2s_set_rate(hz);
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(FACULTY175_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = FACULTY175_I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = FACULTY175_I2S_DMA_FRAME_NUM;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "i2s chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = FACULTY175_I2S_MCLK,
            .bclk = FACULTY175_I2S_BCK,
            .ws = FACULTY175_I2S_WS,
            .dout = FACULTY175_I2S_DOUT,
            .din = FACULTY175_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_16BIT;
    ret = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "i2s tx");

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(hz),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {
            .mclk = FACULTY175_I2S_MCLK,
            .bclk = FACULTY175_I2S_BCK,
            .ws = FACULTY175_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = FACULTY175_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    tdm_cfg.slot_cfg.total_slot = FACULTY175_ES7210_CHANNELS;
    tdm_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    tdm_cfg.slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_32BIT;
    tdm_cfg.slot_cfg.left_align = true;
    ret = i2s_channel_init_tdm_mode(s_i2s_rx, &tdm_cfg);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "i2s rx tdm");
    ret = i2s_channel_enable(s_i2s_tx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "i2s tx en");
    ret = i2s_channel_enable(s_i2s_rx);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "i2s rx en");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = FACULTY175_I2S_PORT,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_GOTO_ON_FALSE(s_i2s_data_if != NULL, ESP_ERR_NO_MEM, fail, TAG, "i2s data if");
    return ESP_OK;

fail:
    if (s_i2s_rx != NULL) {
        (void)i2s_del_channel(s_i2s_rx);
        s_i2s_rx = NULL;
    }
    if (s_i2s_tx != NULL) {
        (void)i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
    }
    return ret;
}

static esp_codec_dev_handle_t faculty175_spk_codec_init(void)
{
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    const audio_codec_ctrl_if_t *i2c_ctrl_if = faculty175_codec_ctrl_new(FACULTY175_ES8311_ADDR >> 1);
    if (i2c_ctrl_if == NULL) {
        return NULL;
    }

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0f,
        .codec_dac_voltage = 3.3f,
    };
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = FACULTY175_PA_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    if (es8311_dev == NULL) {
        return NULL;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };
    return esp_codec_dev_new(&codec_dev_cfg);
}

static esp_codec_dev_handle_t faculty175_mic_codec_init(void)
{
    const audio_codec_ctrl_if_t *i2c_ctrl_if = faculty175_codec_ctrl_new(FACULTY175_ES7210_ADDR_7BIT);
    if (i2c_ctrl_if == NULL) {
        return NULL;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
        .mclk_src = ES7210_MCLK_FROM_PAD,
    };
    const audio_codec_if_t *es7210_dev = es7210_codec_new(&es7210_cfg);
    if (es7210_dev == NULL) {
        return NULL;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7210_dev,
        .data_if = s_i2s_data_if,
    };
    return esp_codec_dev_new(&codec_dev_cfg);
}

static esp_err_t faculty175_es7210_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return faculty175_i2c_hw_write(FACULTY175_ES7210_ADDR_7BIT, data, sizeof(data));
}

static bool faculty175_i2c_probe_silent(uint8_t addr_7bit, int timeout_ms)
{
    if (faculty175_i2c_init() != ESP_OK || s_i2c_bus == NULL) {
        return false;
    }
    return i2c_master_probe(s_i2c_bus, addr_7bit, timeout_ms) == ESP_OK;
}

static bool faculty175_i2c_probe_recovering(uint8_t addr_7bit, int timeout_ms)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (faculty175_i2c_probe_silent(addr_7bit, timeout_ms)) {
            return true;
        }
        faculty175_i2c_reset_bus("i2c-probe-recover");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

typedef struct {
    audio_codec_ctrl_if_t base;
    uint8_t addr_7bit;
    bool open;
} faculty175_codec_ctrl_t;

static int faculty175_codec_ctrl_open(const audio_codec_ctrl_if_t *ctrl, void *cfg, int cfg_size)
{
    (void)cfg;
    (void)cfg_size;
    faculty175_codec_ctrl_t *self = (faculty175_codec_ctrl_t *)ctrl;
    if (self == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    self->open = true;
    return ESP_CODEC_DEV_OK;
}

static bool faculty175_codec_ctrl_is_open(const audio_codec_ctrl_if_t *ctrl)
{
    const faculty175_codec_ctrl_t *self = (const faculty175_codec_ctrl_t *)ctrl;
    return self != NULL && self->open;
}

static int faculty175_codec_ctrl_read(const audio_codec_ctrl_if_t *ctrl,
                                      int reg,
                                      int reg_len,
                                      void *data,
                                      int data_len)
{
    const faculty175_codec_ctrl_t *self = (const faculty175_codec_ctrl_t *)ctrl;
    if (self == NULL || data == NULL || data_len <= 0 || reg_len <= 0 || reg_len > 2) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint8_t reg_buf[2];
    for (int i = 0; i < reg_len; ++i) {
        reg_buf[i] = (uint8_t)(reg >> ((reg_len - i - 1) * 8));
    }
    return faculty175_i2c_hw_write_read(self->addr_7bit, reg_buf, (size_t)reg_len, data, (size_t)data_len) == ESP_OK
               ? ESP_CODEC_DEV_OK
               : ESP_CODEC_DEV_READ_FAIL;
}

static int faculty175_codec_ctrl_write(const audio_codec_ctrl_if_t *ctrl,
                                       int reg,
                                       int reg_len,
                                       void *data,
                                       int data_len)
{
    const faculty175_codec_ctrl_t *self = (const faculty175_codec_ctrl_t *)ctrl;
    if (self == NULL || (data == NULL && data_len > 0) || data_len < 0 || reg_len <= 0 || reg_len > 2) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    uint8_t buf[18];
    if ((size_t)reg_len + (size_t)data_len > sizeof(buf)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    for (int i = 0; i < reg_len; ++i) {
        buf[i] = (uint8_t)(reg >> ((reg_len - i - 1) * 8));
    }
    if (data_len > 0) {
        memcpy(buf + reg_len, data, (size_t)data_len);
    }
    return faculty175_i2c_hw_write(self->addr_7bit, buf, (size_t)reg_len + (size_t)data_len) == ESP_OK
               ? ESP_CODEC_DEV_OK
               : ESP_CODEC_DEV_WRITE_FAIL;
}

static int faculty175_codec_ctrl_close(const audio_codec_ctrl_if_t *ctrl)
{
    faculty175_codec_ctrl_t *self = (faculty175_codec_ctrl_t *)ctrl;
    if (self == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    self->open = false;
    return ESP_CODEC_DEV_OK;
}

static const audio_codec_ctrl_if_t *faculty175_codec_ctrl_new(uint8_t addr_7bit)
{
    faculty175_codec_ctrl_t *ctrl = heap_caps_calloc(1, sizeof(*ctrl), MALLOC_CAP_DEFAULT);
    if (ctrl == NULL) {
        return NULL;
    }
    ctrl->base.open = faculty175_codec_ctrl_open;
    ctrl->base.is_open = faculty175_codec_ctrl_is_open;
    ctrl->base.read_reg = faculty175_codec_ctrl_read;
    ctrl->base.write_reg = faculty175_codec_ctrl_write;
    ctrl->base.close = faculty175_codec_ctrl_close;
    ctrl->addr_7bit = addr_7bit;
    ctrl->open = true;
    return &ctrl->base;
}

static void faculty175_es7210_apply_arduino_compat(void)
{
    esp_err_t err = faculty175_es7210_write_reg(FACULTY175_ES7210_MODE_CONFIG_REG08, 0x20);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES7210 reg08 compat write failed: %s", esp_err_to_name(err));
    }
    err = faculty175_es7210_write_reg(FACULTY175_ES7210_SDP_INTERFACE2_REG12, 0x02);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES7210 reg12 compat write failed: %s", esp_err_to_name(err));
    }
}

static void faculty175_es8311_apply_output_route(void)
{
    if (s_spk_codec == NULL) {
        return;
    }
    const struct {
        int reg;
        int val;
    } route[] = {
        {FACULTY175_ES8311_SYSTEM_REG0D, 0x01},
        {FACULTY175_ES8311_SDPIN_REG09, 0x0C},
        {FACULTY175_ES8311_SDPOUT_REG0A, 0x0C},
        {FACULTY175_ES8311_SYSTEM_REG12, 0x00},
        {FACULTY175_ES8311_SYSTEM_REG13, 0x10},
        {FACULTY175_ES8311_DAC_REG37, 0x08},
        {FACULTY175_ES8311_DAC_REG32, 0xFF},
        {FACULTY175_ES8311_DAC_REG31, 0x00},
    };
    for (size_t i = 0; i < sizeof(route) / sizeof(route[0]); ++i) {
        const int err = esp_codec_dev_write_reg(s_spk_codec, route[i].reg, route[i].val);
        if (err != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG,
                     "ES8311 output route reg 0x%02x <- 0x%02x failed: %d",
                     route[i].reg,
                     route[i].val,
                     err);
            return;
        }
    }
}

static void faculty175_mic_apply_capture_config(void)
{
    if (s_mic_codec == NULL) {
        return;
    }
    (void)esp_codec_dev_set_in_channel_gain(s_mic_codec, codec_channel_mask(0) | codec_channel_mask(1), 24.0f);
    (void)esp_codec_dev_set_in_channel_gain(s_mic_codec, codec_channel_mask(2) | codec_channel_mask(3), 37.5f);
    faculty175_es7210_apply_arduino_compat();
}

static esp_err_t faculty175_codec_open(bool out, uint32_t hz)
{
    esp_codec_dev_handle_t dev = out ? s_spk_codec : s_mic_codec;
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = out ? 2 : FACULTY175_ES7210_CHANNELS,
        .channel_mask = out ? 0 : FACULTY175_ES7210_CHANNEL_MASK,
        .sample_rate = hz == 0 ? FACULTY175_AUDIO_RATE : hz,
    };
    if (out) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_open(dev, &fs), TAG, "spk open");
        faculty175_es8311_apply_output_route();
        return ESP_OK;
    }
    return esp_codec_dev_open(dev, &fs);
}

static esp_err_t faculty175_audio_prepare_capture_locked(void)
{
    if (s_mic_open) {
        return ESP_OK;
    }
    esp_err_t open_err = faculty175_codec_open(false, FACULTY175_AUDIO_RATE);
    if (open_err != ESP_OK) {
        return open_err;
    }
    faculty175_mic_apply_capture_config();
    s_mic_open = true;
    return ESP_OK;
}

static void faculty175_audio_suspend_capture_locked(void)
{
    if (s_mic_open && s_mic_codec != NULL) {
        (void)esp_codec_dev_close(s_mic_codec);
        s_mic_open = false;
    }
}

static esp_err_t faculty175_audio_restart_tx_locked(void)
{
    if (s_i2s_tx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2s_channel_disable(s_i2s_tx);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = i2s_channel_enable(s_i2s_tx);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    return ESP_OK;
}

static esp_err_t faculty175_audio_write_mono_from_stereo(const int16_t *samples,
                                                         size_t sample_count,
                                                         uint32_t timeout_ms)
{
    const size_t bytes = sample_count * sizeof(int16_t);
    size_t written = 0;
    if (s_i2s_tx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const TickType_t ticks = pdMS_TO_TICKS(timeout_ms == 0 ? 200 : timeout_ms);
    esp_err_t err = i2s_channel_write(s_i2s_tx, samples, bytes, &written, ticks);
    if (err != ESP_OK) {
        return err;
    }
    if (written != bytes) {
        ESP_LOGW(TAG, "spk i2s short write %u/%u", (unsigned)written, (unsigned)bytes);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t faculty175_board_play_boot_chime(void)
{
    static const float tone_hz = 220.0f;
    static const float harmonics[] = {1.0f, 2.0f, 3.0f, 4.9f};
    static const float harmonic_amp[] = {1.0f, 0.20f, 0.08f, 0.04f};
    const uint32_t rate = FACULTY175_AUDIO_RATE;
    const size_t chunk_frames = 128;
    int16_t pcm[chunk_frames * 2u];
    int16_t silence[128 * 2u];
    esp_err_t err = ESP_OK;

    memset(silence, 0, sizeof(silence));
    /*
     * Give the codec and speaker path a quiet moment to settle before the
     * first audible sample. This helps avoid the brief burst of static that
     * can show up when the amplifier wakes up and the first tone lands
     * immediately after open/unmute.
     */
    err = faculty175_audio_write_pcm(silence, chunk_frames * 2u, 120);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(24));
    }

    const size_t frames = (size_t)rate * 2u + (rate / 2u);
    float phase[sizeof(harmonics) / sizeof(harmonics[0])] = {};
    const size_t attack_frames = rate / 20u;
    const size_t tail_frames = rate / 4u;
    for (size_t p = 0; p < sizeof(harmonics) / sizeof(harmonics[0]); ++p) {
        phase[p] = (float)((p + 1u) * 0.61803398875f * 6.28318530718f);
    }
    for (size_t base = 0; base < frames && err == ESP_OK; base += chunk_frames) {
        const size_t todo = frames - base < chunk_frames ? frames - base : chunk_frames;
        for (size_t i = 0; i < todo; ++i) {
            const size_t t = base + i;
            const size_t tail = frames - t;
            float env = expf(-0.22f * ((float)t / (float)rate));
            if (t < attack_frames) {
                env *= (float)t / (float)attack_frames;
            }
            if (tail < tail_frames) {
                env *= (float)tail / (float)tail_frames;
            }
            float sample_f = 0.f;
            for (size_t p = 0; p < sizeof(harmonics) / sizeof(harmonics[0]); ++p) {
                const float step = 6.28318530718f * tone_hz * harmonics[p] / (float)rate;
                phase[p] += step;
                if (phase[p] > 6.28318530718f) {
                    phase[p] -= 6.28318530718f;
                }
                const float bright = (p < 2u) ? 1.f : powf(env, 1.10f);
                sample_f += harmonic_amp[p] * bright * sinf(phase[p]);
            }
            sample_f *= 0.45f * env;
            sample_f = sample_f / (1.f + fabsf(sample_f) * 0.12f);
            const int16_t sample = (int16_t)lrintf(sample_f * 14000.f);
            pcm[i * 2u] = sample;
            pcm[i * 2u + 1u] = sample;
        }
        err = faculty175_audio_write_pcm(pcm, todo * 2u, 120);
    }
    ESP_LOGI(TAG, "boot chime %s", esp_err_to_name(err));
    return err;
}

static int32_t faculty175_audio_probe_peak(void)
{
    if (s_mic_codec == NULL) {
        return 0;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = FACULTY175_ES7210_CHANNELS,
        .channel_mask = FACULTY175_ES7210_CHANNEL_MASK,
        .sample_rate = FACULTY175_AUDIO_RATE,
    };
    if (esp_codec_dev_open(s_mic_codec, &fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_gain(s_mic_codec, 30.0f);

    int16_t frame[320 * FACULTY175_ES7210_CHANNELS];
    int32_t peak = 0;
    bool read_ok = false;
    vTaskDelay(pdMS_TO_TICKS(80));
    for (int i = 0; i < 8; ++i) {
        if (esp_codec_dev_read(s_mic_codec, frame, sizeof(frame)) != ESP_OK) {
            continue;
        }
        read_ok = true;
        for (size_t j = 0; j < 320 * FACULTY175_ES7210_CHANNELS; ++j) {
            const int32_t abs = sample_abs(frame[j]);
            if (abs > peak) {
                peak = abs;
            }
        }
    }
    esp_codec_dev_close(s_mic_codec);
    s_mic_open = false;
    return read_ok ? peak : 0;
}

static esp_err_t faculty175_audio_init(void)
{
    g_faculty175_boot_stage = 0xae41;
    g_faculty175_boot_last_err = faculty175_i2c_init();
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "i2c");
    g_faculty175_boot_stage = 0xae42;
    const bool spk_present = faculty175_i2c_probe_recovering(FACULTY175_ES8311_ADDR >> 1, 25);
    const bool mic_present = faculty175_i2c_probe_recovering(FACULTY175_ES7210_ADDR_7BIT, 25);
    if (!spk_present) {
        ESP_LOGW(TAG,
                 "speaker codec absent on I2C (ES8311=%d ES7210=%d); skipping codec init",
                 spk_present,
                 mic_present);
        return ESP_FAIL;
    }
    if (!mic_present) {
        ESP_LOGW(TAG, "ES7210 mic codec absent on I2C; starting speaker-only audio");
    }

    g_faculty175_boot_stage = 0xae43;
    g_faculty175_boot_last_err = faculty175_i2s_init(FACULTY175_AUDIO_RATE);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "i2s");
    if (s_audio_read_mux == NULL) {
        s_audio_read_mux = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_audio_read_mux != NULL, ESP_ERR_NO_MEM, TAG, "audio read mutex");
    }
    g_faculty175_boot_stage = 0xae44;
    if (s_aec_ref == NULL) {
        s_aec_ref = heap_caps_calloc(FACULTY175_AEC_REF_SAMPLES, sizeof(s_aec_ref[0]),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_aec_ref == NULL) {
            ESP_LOGW(TAG, "AEC reference buffer allocation failed; echo cancellation disabled");
        } else {
            ESP_LOGI(TAG, "AEC reference buffer ready (%u samples)",
                     (unsigned)FACULTY175_AEC_REF_SAMPLES);
        }
    }

    g_faculty175_boot_stage = 0xae45;
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << FACULTY175_PA_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    g_faculty175_boot_last_err = gpio_config(&pa_cfg);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "pa gpio");
    gpio_set_level(FACULTY175_PA_GPIO, 1);

    g_faculty175_boot_stage = 0xae46;
    s_spk_codec = faculty175_spk_codec_init();
    g_faculty175_boot_stage = 0xae47;
    if (mic_present) {
        s_mic_codec = faculty175_mic_codec_init();
    } else {
        s_mic_codec = NULL;
    }
    ESP_RETURN_ON_FALSE(s_spk_codec != NULL && (!mic_present || s_mic_codec != NULL), ESP_FAIL, TAG, "codec init");

    g_faculty175_boot_stage = 0xae48;
    s_spk_open = false;
    s_mic_open = false;
    s_spk_rate_hz = FACULTY175_AUDIO_RATE;
    g_faculty175_boot_stage = 0xae4c;
    s_speaker_ready = true;

    if (!mic_present) {
        return ESP_OK;
    }

    g_faculty175_boot_stage = 0xae4d;
    g_faculty175_boot_last_err = faculty175_audio_prepare_capture_locked();
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "mic open");
    s_mic_probe_peak = 0;
    g_faculty175_boot_stage = 0xae4e;
    g_faculty175_boot_stage = 0xae50;
    return ESP_OK;
}

static void faculty175_lcd_hardware_reset(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << FACULTY175_LCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        return;
    }
    gpio_set_level(FACULTY175_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(FACULTY175_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

static void faculty175_lcd_release_shared_reset(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << FACULTY175_LCD_PIN_RST,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

static esp_err_t faculty175_lcd_init(void)
{
    g_faculty175_boot_stage = 0xae31;
    const spi_bus_config_t bus_cfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        FACULTY175_LCD_PIN_PCLK,
        FACULTY175_LCD_PIN_DATA0,
        FACULTY175_LCD_PIN_DATA1,
        FACULTY175_LCD_PIN_DATA2,
        FACULTY175_LCD_PIN_DATA3,
        FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t));
    g_faculty175_boot_last_err = spi_bus_initialize(FACULTY175_LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "spi bus");
    g_faculty175_boot_stage = 0xae32;

    if (s_flush_done == NULL) {
        s_flush_done = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_flush_done != NULL, ESP_ERR_NO_MEM, TAG, "flush sem");
    }
    if (s_display_lock == NULL) {
        s_display_lock = xSemaphoreCreateRecursiveMutex();
        ESP_RETURN_ON_FALSE(s_display_lock != NULL, ESP_ERR_NO_MEM, TAG, "display lock");
    }
    g_faculty175_boot_stage = 0xae33;

    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(FACULTY175_LCD_PIN_CS, lcd_flush_done_cb, NULL);
    io_cfg.trans_queue_depth = 10;
    io_cfg.pclk_hz = 20 * 1000 * 1000;
    g_faculty175_boot_last_err =
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)FACULTY175_LCD_HOST, &io_cfg, &s_panel_io);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "lcd io");
    g_faculty175_boot_stage = 0xae34;

    faculty175_lcd_hardware_reset();
    g_faculty175_boot_stage = 0xae35;

    co5300_vendor_config_t vendor_cfg = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = FACULTY175_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = &vendor_cfg,
    };
    g_faculty175_boot_last_err = esp_lcd_new_panel_co5300(s_panel_io, &panel_cfg, &s_panel);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "lcd panel");
    g_faculty175_boot_stage = 0xae36;
    g_faculty175_boot_last_err = esp_lcd_panel_reset(s_panel);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "lcd reset");
    g_faculty175_boot_stage = 0xae365;
    g_faculty175_boot_last_err = esp_lcd_panel_set_gap(s_panel, FACULTY175_LCD_PANEL_GAP_X, FACULTY175_LCD_PANEL_GAP_Y);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "lcd gap");
    g_faculty175_boot_stage = 0xae37;
    g_faculty175_boot_last_err = esp_lcd_panel_init(s_panel);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "lcd init");
    g_faculty175_boot_stage = 0xae38;
    g_faculty175_boot_last_err = esp_lcd_panel_disp_on_off(s_panel, true);
    ESP_RETURN_ON_ERROR(g_faculty175_boot_last_err, TAG, "lcd on");
    g_faculty175_boot_stage = 0xae39;
    faculty175_lcd_release_shared_reset();
    g_faculty175_boot_stage = 0xae3a;

    s_fb = heap_caps_malloc(FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(s_fb != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    g_faculty175_boot_stage = 0xae3b;
    faculty175_display_fill_rgb565(faculty175_ui_bg565());
    faculty175_display_flush_fb();
    g_faculty175_boot_stage = 0xae3c;
    faculty175_board_set_backlight(100);
    g_faculty175_boot_stage = 0xae3d;
    ESP_LOGI(TAG, "CO5300 466×466 init ok (1.75C — not SH8601/1.8″)");
    return ESP_OK;
}

void faculty175_display_boot_progress(const char *detail, uint8_t step, uint8_t total, bool active)
{
    if (detail != NULL && detail[0] != '\0') {
        strncpy(s_boot_watch_detail, detail, sizeof(s_boot_watch_detail) - 1u);
        s_boot_watch_detail[sizeof(s_boot_watch_detail) - 1u] = '\0';
    }
    if (total == 0) {
        total = 1;
    }
    if (step > total) {
        step = total;
    }
    s_boot_watch_step = step;
    s_boot_watch_total = total;
    s_boot_watch_progress_active = active;
    s_boot_watch_active = active;
    if (active && s_panel != NULL && s_fb != NULL && s_boot_watch_task == NULL) {
        xTaskCreate(boot_watch_task, "boot_watch", 4096, NULL, 3, &s_boot_watch_task);
    }
    if (!active || s_panel == NULL || s_fb == NULL) {
        return;
    }
    faculty175_display_lock();
    faculty175_display_draw_pocketwatch(s_boot_watch_detail,
                                        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                                        active);
    faculty175_display_unlock();
}

bool faculty175_board_audio_ready(void)
{
    return s_speaker_ready && s_spk_codec != NULL;
}

bool faculty175_board_pi4ioe_ok(void)
{
    return false;
}

int32_t faculty175_board_mic_probe_peak(void)
{
    return s_mic_probe_peak;
}

esp_err_t faculty175_board_init(void)
{
    g_faculty175_boot_stage = 0xae01;
    g_faculty175_boot_last_err = ESP_OK;
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << FACULTY175_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&btn);
    g_faculty175_boot_last_err = err;
    ESP_RETURN_ON_ERROR(err, TAG, "button gpio");

    bool i2c_ready = false;
    bool lcd_ready = false;

    g_faculty175_boot_stage = 0xae02;
    faculty175_log_i2c_gpio_drive_test("boot-pre-i2c-driver");
    gpio_config_t i2c_idle = {
        .pin_bit_mask = (1ULL << FACULTY175_AUDIO_I2C_SDA) | (1ULL << FACULTY175_AUDIO_I2C_SCL),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&i2c_idle);
    g_faculty175_boot_last_err = err;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c idle gpio failed: %s", esp_err_to_name(err));
    } else {
        faculty175_lcd_hardware_reset();
        faculty175_lcd_release_shared_reset();
        faculty175_log_i2c_lines("boot-pre-lcd");
        err = faculty175_i2c_init();
        g_faculty175_boot_last_err = err;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2c pre-lcd failed: %s", esp_err_to_name(err));
        } else {
            i2c_ready = true;
        }
    }

#if FACULTY175_HTTP_SCREEN_QA_SKIP_AUDIO
    s_audio_ready = false;
    ESP_LOGW(TAG, "audio init skipped for HTTP screen QA");
#else
    s_audio_ready = i2c_ready && faculty175_audio_init() == ESP_OK;
    if (!s_audio_ready) {
        ESP_LOGW(TAG, "audio init before LCD unavailable; will retry after display init");
    }
#endif

    g_faculty175_boot_stage = 0xae03;
    err = faculty175_lcd_init();
    g_faculty175_boot_last_err = err;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lcd init failed: %s", esp_err_to_name(err));
    } else {
        lcd_ready = true;
        faculty175_log_i2c_lines("boot-post-lcd");
    }

    g_faculty175_boot_stage = 0xae04;
    if (!s_audio_ready && i2c_ready) {
        s_audio_ready = faculty175_audio_init() == ESP_OK;
    }
    if (!s_audio_ready) {
        ESP_LOGW(TAG, "audio init unavailable — continuing without speaker/mic");
    }

    g_faculty175_boot_stage = 0xae05;
    if (!i2c_ready || faculty175_pmu_init() != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 PMU init failed — audio/display may be unavailable");
    }
    g_faculty175_boot_stage = 0xae06;
    faculty175_board_log_identity();
    vTaskDelay(pdMS_TO_TICKS(50));

    g_faculty175_boot_stage = 0xae07;
    if (i2c_ready) {
        (void)faculty175_touch_init();
    } else {
        ESP_LOGW(TAG, "touch init skipped: shared I2C unavailable");
    }
    g_faculty175_boot_stage = 0xae08;
    ESP_LOGI(TAG,
             "Faculty175 ready (lcd=%s audio=%s i2c=%s)",
             lcd_ready ? "ok" : "off",
             s_audio_ready ? "ok" : "off",
             i2c_ready ? "ok" : "off");
    return ESP_OK;
}

void faculty175_board_set_backlight(uint8_t percent)
{
    if (s_panel_io == NULL) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    const uint8_t brightness = (uint8_t)(percent * 255 / 100);
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    (void)esp_lcd_panel_io_tx_param(s_panel_io, lcd_cmd, &brightness, 1);
}

void faculty175_board_display_on(bool on)
{
    if (s_panel == NULL) {
        return;
    }
    if (!on) {
        faculty175_board_set_backlight(0);
    }
    (void)esp_lcd_panel_disp_on_off(s_panel, on);
    if (on) {
        faculty175_board_set_backlight(100);
    }
}

esp_err_t faculty175_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    if (!s_audio_ready || s_mic_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sample_count > FACULTY175_AUDIO_MAX_READ_SAMPLES) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_audio_read_mux != NULL &&
        xSemaphoreTake(s_audio_read_mux, pdMS_TO_TICKS(timeout_ms == 0 ? 200 : timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t capture_err = faculty175_audio_prepare_capture_locked();
    if (capture_err != ESP_OK) {
        if (s_audio_read_mux != NULL) {
            xSemaphoreGive(s_audio_read_mux);
        }
        return capture_err;
    }

    const size_t groups = sample_count * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME;
    const size_t bytes = groups * FACULTY175_ES7210_CHANNELS * sizeof(int16_t);
    if (esp_codec_dev_read(s_mic_codec, s_audio_read_tdm, bytes) != ESP_OK) {
        if (s_audio_read_mux != NULL) {
            xSemaphoreGive(s_audio_read_mux);
        }
        return ESP_FAIL;
    }

    uint64_t group_energy[FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME] = {0};
    for (size_t i = 0; i < sample_count; ++i) {
        for (size_t group = 0; group < FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME; ++group) {
            const size_t frame_base = (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + group) * FACULTY175_ES7210_CHANNELS;
            for (int ch = 0; ch < FACULTY175_ES7210_CHANNELS; ++ch) {
                const int32_t sample = audio_sample_host_order(s_audio_read_tdm[frame_base + ch]);
                group_energy[group] += (uint64_t)(sample * sample);
            }
        }
    }
    size_t populated_group = 0;
    for (size_t group = 1; group < FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME; ++group) {
        if (group_energy[group] > group_energy[populated_group]) {
            populated_group = group;
        }
    }

    int64_t sum[FACULTY175_ES7210_CHANNELS] = {0};
    for (size_t i = 0; i < sample_count; ++i) {
        const size_t frame_base =
            (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + populated_group) * FACULTY175_ES7210_CHANNELS;
        for (int ch = 0; ch < FACULTY175_ES7210_CHANNELS; ++ch) {
            sum[ch] += audio_sample_host_order(s_audio_read_tdm[frame_base + ch]);
        }
    }

    int32_t mean[FACULTY175_ES7210_CHANNELS] = {0};
    for (int ch = 0; ch < FACULTY175_ES7210_CHANNELS; ++ch) {
        mean[ch] = (int32_t)(sum[ch] / (int64_t)sample_count);
    }

    uint64_t energy[FACULTY175_ES7210_CHANNELS] = {0};
    uint32_t peak[FACULTY175_ES7210_CHANNELS] = {0};
    for (size_t i = 0; i < sample_count; ++i) {
        const size_t frame_base =
            (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + populated_group) * FACULTY175_ES7210_CHANNELS;
        for (int ch = 0; ch < FACULTY175_ES7210_CHANNELS; ++ch) {
            int32_t sample = (int32_t)audio_sample_host_order(s_audio_read_tdm[frame_base + ch]) - mean[ch];
            energy[ch] += (uint64_t)(sample * sample);
            if (sample < 0) {
                sample = -sample;
            }
            if ((uint32_t)sample > peak[ch]) {
                peak[ch] = (uint32_t)sample;
            }
        }
    }

    const int mic_a = FACULTY175_ES7210_MIC_A_CH;
    const int mic_b = FACULTY175_ES7210_MIC_B_CH;
    uint64_t sum_mix_energy = 0;
    uint64_t diff_mix_energy = 0;
    uint32_t sum_mix_peak = 0;
    uint32_t diff_mix_peak = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        const size_t frame_base =
            (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + populated_group) * FACULTY175_ES7210_CHANNELS;
        const int32_t sample_a =
            (int32_t)audio_sample_host_order(s_audio_read_tdm[frame_base + mic_a]) - mean[mic_a];
        const int32_t sample_b =
            (int32_t)audio_sample_host_order(s_audio_read_tdm[frame_base + mic_b]) - mean[mic_b];
        const int32_t sum_mix = (sample_a + sample_b) / 2;
        const int32_t diff_mix = (sample_a - sample_b) / 2;
        const uint32_t sum_abs = (uint32_t)(sum_mix < 0 ? -sum_mix : sum_mix);
        const uint32_t diff_abs = (uint32_t)(diff_mix < 0 ? -diff_mix : diff_mix);
        sum_mix_energy += (uint64_t)(sum_mix * sum_mix);
        diff_mix_energy += (uint64_t)(diff_mix * diff_mix);
        if (sum_abs > sum_mix_peak) {
            sum_mix_peak = sum_abs;
        }
        if (diff_abs > diff_mix_peak) {
            diff_mix_peak = diff_abs;
        }
    }
    int mix_mode = 0;
    uint32_t mix_peak = sum_mix_peak;
    const bool mic_a_dominant =
        peak[mic_a] > 96u && (peak[mic_b] < 32u || peak[mic_a] >= peak[mic_b] * 4u);
    const bool mic_b_dominant =
        peak[mic_b] > 96u && (peak[mic_a] < 32u || peak[mic_b] >= peak[mic_a] * 4u);
    if (mic_a_dominant || mic_b_dominant) {
        mix_mode = 4;
        mix_peak = peak[mic_a] > peak[mic_b] ? peak[mic_a] : peak[mic_b];
    } else if (mix_peak < 32u && (peak[mic_a] > 32u || peak[mic_b] > 32u)) {
        mix_mode = peak[mic_b] > peak[mic_a] ? 3 : 2;
        mix_peak = peak[mix_mode == 3 ? mic_b : mic_a];
    }

    if (s_audio_selected_ch != -2) {
        s_audio_selected_ch = -2;
        ESP_LOGI(TAG, "mic selected ES7210 group=%u dual ch%d/ch%d mode=%s peak=%u/%u",
                 (unsigned)populated_group,
                 mic_a,
                 mic_b,
                 mix_mode == 1 ? "diff"
                               : (mix_mode == 2 ? "chA"
                                                : (mix_mode == 3 ? "chB" : (mix_mode == 4 ? "weighted" : "sum"))),
                 (unsigned)peak[mic_a],
                 (unsigned)peak[mic_b]);
    } else if ((++s_audio_read_frames % 25u) == 0u && mix_peak > 96u) {
        ESP_LOGI(TAG, "mic mix group=%u ch%d/ch%d mode=%s peak=%u/%u p=%u/%u/%u/%u e=%u/%u/%u/%u",
                 (unsigned)populated_group,
                 mic_a,
                 mic_b,
                 mix_mode == 1 ? "diff"
                               : (mix_mode == 2 ? "chA"
                                                : (mix_mode == 3 ? "chB" : (mix_mode == 4 ? "weighted" : "sum"))),
                 (unsigned)peak[mic_a],
                 (unsigned)peak[mic_b],
                 (unsigned)peak[0],
                 (unsigned)peak[1],
                 (unsigned)peak[2],
                 (unsigned)peak[3],
                 (unsigned)(energy[0] / sample_count),
                 (unsigned)(energy[1] / sample_count),
                 (unsigned)(energy[2] / sample_count),
                 (unsigned)(energy[3] / sample_count));
    }

    for (size_t i = 0; i < sample_count; ++i) {
        const size_t frame_base =
            (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + populated_group) * FACULTY175_ES7210_CHANNELS;
        const int32_t sample_a =
            (int32_t)audio_sample_host_order(s_audio_read_tdm[frame_base + mic_a]) - mean[mic_a];
        const int32_t sample_b =
            (int32_t)audio_sample_host_order(s_audio_read_tdm[frame_base + mic_b]) - mean[mic_b];
        int32_t sample = 0;
        if (mix_mode == 1) {
            sample = (sample_a - sample_b) / 2;
        } else if (mix_mode == 2) {
            sample = sample_a;
        } else if (mix_mode == 3) {
            sample = sample_b;
        } else if (mix_mode == 4) {
            const uint32_t weight_a = peak[mic_a] + 1u;
            const uint32_t weight_b = peak[mic_b] + 1u;
            sample = (int32_t)(((int64_t)sample_a * (int64_t)weight_a + (int64_t)sample_b * (int64_t)weight_b) /
                               (int64_t)(weight_a + weight_b));
        } else {
            sample = (sample_a + sample_b) / 2;
        }
        sample *= FACULTY175_MIC_MONO_GAIN;
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }
        samples[i] = (int16_t)sample;
    }
    faculty175_aec_process(samples, sample_count);
    if (out_read != NULL) {
        *out_read = sample_count;
    }
    if (s_audio_read_mux != NULL) {
        xSemaphoreGive(s_audio_read_mux);
    }
    return ESP_OK;
}

esp_err_t faculty175_audio_prepare_capture(uint32_t timeout_ms)
{
    if (!s_audio_ready || s_mic_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_audio_read_mux != NULL &&
        xSemaphoreTake(s_audio_read_mux, pdMS_TO_TICKS(timeout_ms == 0 ? 200 : timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = faculty175_audio_prepare_capture_locked();
    if (s_audio_read_mux != NULL) {
        xSemaphoreGive(s_audio_read_mux);
    }
    return err;
}

esp_err_t faculty175_audio_read_tdm_raw(int16_t *samples,
                                        size_t frame_count,
                                        size_t *out_frames,
                                        uint32_t timeout_ms)
{
    if (!s_audio_ready || s_mic_codec == NULL || samples == NULL || frame_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_count > FACULTY175_AUDIO_MAX_READ_SAMPLES) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_audio_read_mux != NULL &&
        xSemaphoreTake(s_audio_read_mux, pdMS_TO_TICKS(timeout_ms == 0 ? 200 : timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t capture_err = faculty175_audio_prepare_capture_locked();
    if (capture_err != ESP_OK) {
        if (s_audio_read_mux != NULL) {
            xSemaphoreGive(s_audio_read_mux);
        }
        return capture_err;
    }

    const size_t groups = frame_count * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME;
    const size_t tdm_samples = groups * FACULTY175_ES7210_CHANNELS;
    const size_t bytes = tdm_samples * sizeof(int16_t);
    if (esp_codec_dev_read(s_mic_codec, s_audio_read_tdm, bytes) != ESP_OK) {
        if (s_audio_read_mux != NULL) {
            xSemaphoreGive(s_audio_read_mux);
        }
        return ESP_FAIL;
    }
    uint64_t group_energy[FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME] = {0};
    for (size_t i = 0; i < frame_count; ++i) {
        for (size_t group = 0; group < FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME; ++group) {
            const size_t src_base = (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + group) * FACULTY175_ES7210_CHANNELS;
            for (int ch = 0; ch < FACULTY175_ES7210_CHANNELS; ++ch) {
                const int32_t sample = audio_sample_host_order(s_audio_read_tdm[src_base + (size_t)ch]);
                group_energy[group] += (uint64_t)(sample * sample);
            }
        }
    }
    size_t populated_group = 0;
    for (size_t group = 1; group < FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME; ++group) {
        if (group_energy[group] > group_energy[populated_group]) {
            populated_group = group;
        }
    }
    for (size_t i = 0; i < frame_count; ++i) {
        const size_t src_base =
            (i * FACULTY175_ES7210_HALFWORD_GROUPS_PER_FRAME + populated_group) * FACULTY175_ES7210_CHANNELS;
        const size_t dst_base = i * FACULTY175_ES7210_CHANNELS;
        for (int ch = 0; ch < FACULTY175_ES7210_CHANNELS; ++ch) {
            samples[dst_base + (size_t)ch] = audio_sample_host_order(s_audio_read_tdm[src_base + (size_t)ch]);
        }
    }
    if (out_frames != NULL) {
        *out_frames = frame_count;
    }
    if (s_audio_read_mux != NULL) {
        xSemaphoreGive(s_audio_read_mux);
    }
    return ESP_OK;
}

esp_err_t faculty175_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    if (samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_spk_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_read_mux != NULL &&
        xSemaphoreTake(s_audio_read_mux, pdMS_TO_TICKS(timeout_ms == 0 ? 200 : timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "spk write mutex timeout samples=%u open=%d", (unsigned)sample_count, s_spk_open ? 1 : 0);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_OK;
    bool opened_speaker = false;
    faculty175_audio_set_speaker_pa_level(true);
    faculty175_audio_suspend_capture_locked();
    if (!s_spk_open) {
        err = faculty175_codec_open(true, s_spk_rate_hz);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spk open: %s", esp_err_to_name(err));
            goto out;
        }
        err = esp_codec_dev_set_out_vol(s_spk_codec, FACULTY175_SPEAKER_VOLUME);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spk vol: %s", esp_err_to_name(err));
            goto out;
        }
        err = esp_codec_dev_set_out_mute(s_spk_codec, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spk unmute: %s", esp_err_to_name(err));
            goto out;
        }
        s_spk_open = true;
        opened_speaker = true;
    }
    if (opened_speaker) {
        err = faculty175_audio_restart_tx_locked();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spk tx restart: %s", esp_err_to_name(err));
            goto out;
        }
    }

    faculty175_aec_push_reference(samples, sample_count);
    err = faculty175_audio_write_mono_from_stereo(samples, sample_count, timeout_ms);

out:
    if (s_audio_read_mux != NULL) {
        xSemaphoreGive(s_audio_read_mux);
    }
    return err;
}

esp_err_t faculty175_audio_reset_speaker(uint32_t timeout_ms)
{
    if (s_spk_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_audio_read_mux != NULL &&
        xSemaphoreTake(s_audio_read_mux, pdMS_TO_TICKS(timeout_ms == 0 ? 200 : timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "spk reset mutex timeout open=%d", s_spk_open ? 1 : 0);
        return ESP_ERR_TIMEOUT;
    }
    (void)esp_codec_dev_close(s_spk_codec);
    s_spk_open = false;
    if (s_audio_read_mux != NULL) {
        xSemaphoreGive(s_audio_read_mux);
    }
    return ESP_OK;
}

esp_err_t faculty175_audio_set_sample_rate(uint32_t hz)
{
    if (s_spk_codec == NULL || s_mic_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (hz == 0) {
        hz = FACULTY175_AUDIO_RATE;
    }
    (void)esp_codec_dev_close(s_spk_codec);
    s_spk_open = false;
    (void)esp_codec_dev_close(s_mic_codec);
    s_mic_open = false;
    faculty175_aec_reset(hz);
    ESP_RETURN_ON_ERROR(faculty175_i2s_set_rate(hz), TAG, "i2s rate");
    s_spk_rate_hz = hz;
    s_spk_open = false;
    ESP_RETURN_ON_ERROR(faculty175_codec_open(false, hz), TAG, "mic open");
    s_mic_open = true;
    faculty175_mic_apply_capture_config();
    return ESP_OK;
}

void faculty175_audio_set_speaker_mute(bool mute)
{
    faculty175_audio_set_speaker_pa_level(!mute);
    if (s_spk_codec != NULL) {
        (void)esp_codec_dev_set_out_mute(s_spk_codec, mute);
    }
}

void faculty175_audio_set_speaker_pa_level(bool enabled)
{
    gpio_set_direction(FACULTY175_PA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(FACULTY175_PA_GPIO, enabled ? 1 : 0);
}

void faculty175_audio_set_speaker_volume(uint8_t volume)
{
    if (s_spk_codec == NULL) {
        return;
    }
    if (volume > 100u) {
        volume = 100u;
    }
    (void)esp_codec_dev_set_out_vol(s_spk_codec, volume);
}

void faculty175_display_fill_rgb565(uint16_t color)
{
    if (s_fb == NULL) {
        return;
    }
    for (int i = 0; i < FACULTY175_LCD_W * FACULTY175_LCD_H; ++i) {
        s_fb[i] = color;
    }
}

void faculty175_display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (s_fb == NULL) {
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
    if (x + w > FACULTY175_LCD_W) {
        w = FACULTY175_LCD_W - x;
    }
    if (y + h > FACULTY175_LCD_H) {
        h = FACULTY175_LCD_H - y;
    }
    const bool cooperative_fill = (w * h) >= (FACULTY175_LCD_W * 32);
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            s_fb[row * FACULTY175_LCD_W + col] = color;
        }
        if (cooperative_fill && (row & 0x0f) == 0x0f) {
            vTaskDelay(1);
        }
    }
}

void faculty175_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    faculty175_display_blit_rgb565_masked(pixels, NULL, x, y, w, h);
}

void faculty175_display_draw_rgb565_stride(const uint16_t *pixels, int src_stride_pixels, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL || src_stride_pixels <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (src_stride_pixels == w) {
        faculty175_display_draw_rgb565(pixels, x, y, w, h);
        return;
    }

    int src_x = 0;
    int src_y = 0;
    if (x < 0) {
        src_x = -x;
        w += x;
        x = 0;
    }
    if (y < 0) {
        src_y = -y;
        h += y;
        y = 0;
    }
    if (x >= FACULTY175_LCD_W || y >= FACULTY175_LCD_H || w <= 0 || h <= 0) {
        return;
    }
    if (x + w > FACULTY175_LCD_W) {
        w = FACULTY175_LCD_W - x;
    }
    if (y + h > FACULTY175_LCD_H) {
        h = FACULTY175_LCD_H - y;
    }

    for (int row = 0; row < h; ++row) {
        memcpy(&s_fb[(y + row) * FACULTY175_LCD_W + x],
               &pixels[(src_y + row) * src_stride_pixels + src_x],
               (size_t)w * sizeof(uint16_t));
    }
}

void faculty175_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL) {
        return;
    }
    if (opaque == NULL && x >= 0 && y >= 0 &&
        x + w <= FACULTY175_LCD_W && y + h <= FACULTY175_LCD_H) {
        for (int row = 0; row < h; ++row) {
            memcpy(&s_fb[(y + row) * FACULTY175_LCD_W + x],
                   &pixels[row * w],
                   (size_t)w * sizeof(uint16_t));
        }
        return;
    }
    for (int row = 0; row < h; ++row) {
        if (y + row < 0 || y + row >= FACULTY175_LCD_H) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            if (x + col < 0 || x + col >= FACULTY175_LCD_W) {
                continue;
            }
            const int idx = row * w + col;
            if (opaque != NULL) {
                const uint8_t a = opaque[idx];
                if (a == 0) {
                    continue;
                }
                if (a < 255) {
                    const int fi = (y + row) * FACULTY175_LCD_W + (x + col);
                    const uint16_t bg = s_fb[fi];
                    const uint16_t fg = pixels[idx];
                    const uint8_t br = (uint8_t)(((bg >> 11) & 0x1f) * 255 / 31);
                    const uint8_t bg_g = (uint8_t)(((bg >> 5) & 0x3f) * 255 / 63);
                    const uint8_t bb = (uint8_t)((bg & 0x1f) * 255 / 31);
                    const uint8_t fr = (uint8_t)(((fg >> 11) & 0x1f) * 255 / 31);
                    const uint8_t fg_g = (uint8_t)(((fg >> 5) & 0x3f) * 255 / 63);
                    const uint8_t fb = (uint8_t)((fg & 0x1f) * 255 / 31);
                    const uint16_t ia = (uint16_t)(255u - a);
                    s_fb[fi] = rgb565((uint8_t)((br * ia + fr * a) / 255u),
                                      (uint8_t)((bg_g * ia + fg_g * a) / 255u),
                                      (uint8_t)((bb * ia + fb * a) / 255u));
                    continue;
                }
            }
            s_fb[(y + row) * FACULTY175_LCD_W + (x + col)] = pixels[idx];
        }
    }
}

static void draw_char5x7(char c, int x, int y, uint16_t color)
{
    static const uint8_t font[95][5] = {
        {0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x5F, 0x00, 0x00},
        {0x00, 0x07, 0x00, 0x07, 0x00},
        {0x14, 0x7F, 0x14, 0x7F, 0x14},
        {0x24, 0x2A, 0x7F, 0x2A, 0x12},
        {0x23, 0x13, 0x08, 0x64, 0x62},
        {0x36, 0x49, 0x55, 0x22, 0x50},
        {0x00, 0x05, 0x03, 0x00, 0x00},
        {0x00, 0x1C, 0x22, 0x41, 0x00},
        {0x00, 0x41, 0x22, 0x1C, 0x00},
        {0x14, 0x08, 0x3E, 0x08, 0x14},
        {0x08, 0x08, 0x3E, 0x08, 0x08},
        {0x00, 0x50, 0x30, 0x00, 0x00},
        {0x08, 0x08, 0x08, 0x08, 0x08},
        {0x00, 0x60, 0x60, 0x00, 0x00},
        {0x20, 0x10, 0x08, 0x04, 0x02},
        {0x3E, 0x51, 0x49, 0x45, 0x3E},
        {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1E},
        {0x00, 0x36, 0x36, 0x00, 0x00},
        {0x00, 0x56, 0x36, 0x00, 0x00},
        {0x08, 0x14, 0x22, 0x41, 0x00},
        {0x14, 0x14, 0x14, 0x14, 0x14},
        {0x00, 0x41, 0x22, 0x14, 0x08},
        {0x02, 0x01, 0x51, 0x09, 0x06},
        {0x32, 0x49, 0x79, 0x41, 0x3E},
        {0x7E, 0x11, 0x11, 0x11, 0x7E},
        {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22},
        {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41},
        {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A},
        {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00},
        {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41},
        {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F},
        {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E},
        {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E},
        {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31},
        {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F},
        {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x3F, 0x40, 0x38, 0x40, 0x3F},
        {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07},
        {0x61, 0x51, 0x49, 0x45, 0x43},
    };
    if (c < 32 || c > 126) {
        c = '?';
    }
    const uint8_t *glyph = font[c - 32];
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if (bits & (1 << row)) {
                faculty175_display_fill_rect(x + col, y + row, 1, 1, color);
            }
        }
    }
}

static void draw_text(const char *text, int x, int y, uint16_t color)
{
    if (text == NULL) {
        return;
    }
    int cx = x;
    for (const char *p = text; *p != '\0'; ++p) {
        draw_char5x7(*p, cx, y, color);
        cx += 6;
    }
}

static void draw_centered_text(const char *text, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    int x = (FACULTY175_LCD_W - w) / 2;
    if (x < 0) {
        x = 0;
    }
    draw_text(text, x, y, color);
}

static void draw_text_center_at(const char *text, int cx, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    draw_text(text, cx - w / 2, y, color);
}

static void draw_thick_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    draw_line_safe(x0, y0, x1, y1, color);
    draw_line_safe(x0 + 1, y0, x1 + 1, y1, color);
    draw_line_safe(x0, y0 + 1, x1, y1 + 1, color);
}

static void draw_radial_hand(int cx, int cy, float angle, int inner_r, int outer_r, uint16_t color, bool thick)
{
    const int x0 = cx + (int)lrintf(cosf(angle) * (float)inner_r);
    const int y0 = cy + (int)lrintf(sinf(angle) * (float)inner_r);
    const int x1 = cx + (int)lrintf(cosf(angle) * (float)outer_r);
    const int y1 = cy + (int)lrintf(sinf(angle) * (float)outer_r);
    if (thick) {
        draw_thick_line(x0, y0, x1, y1, color);
    } else {
        draw_line_safe(x0, y0, x1, y1, color);
    }
}

static void draw_radial_hand_width(int cx,
                                   int cy,
                                   float angle,
                                   int inner_r,
                                   int outer_r,
                                   uint16_t color,
                                   int width_px)
{
    const float ca = cosf(angle);
    const float sa = sinf(angle);
    const float nx = -sa;
    const float ny = ca;
    const int x0 = cx + (int)lrintf(ca * (float)inner_r);
    const int y0 = cy + (int)lrintf(sa * (float)inner_r);
    const int x1 = cx + (int)lrintf(ca * (float)outer_r);
    const int y1 = cy + (int)lrintf(sa * (float)outer_r);
    const int half = width_px / 2;
    for (int o = -half; o <= half; ++o) {
        const int ox = (int)lrintf(nx * (float)o);
        const int oy = (int)lrintf(ny * (float)o);
        draw_line_safe(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
    }
}

typedef struct {
    int x;
    int y;
} point_i_t;

static int edge_i(point_i_t a, point_i_t b, int x, int y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

static void fill_triangle_points(point_i_t a, point_i_t b, point_i_t c, uint16_t color)
{
    int min_x = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    int max_x = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    int min_y = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    int max_y = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
    if (min_x < 0) {
        min_x = 0;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_x >= FACULTY175_LCD_W) {
        max_x = FACULTY175_LCD_W - 1;
    }
    if (max_y >= FACULTY175_LCD_H) {
        max_y = FACULTY175_LCD_H - 1;
    }
    const int area = edge_i(a, b, c.x, c.y);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int w0 = edge_i(b, c, x, y);
            const int w1 = edge_i(c, a, x, y);
            const int w2 = edge_i(a, b, x, y);
            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                faculty175_display_draw_pixel(x, y, color);
            }
        }
    }
}

static point_i_t hand_point(int cx, int cy, float ca, float sa, float nx, float ny, int r, int half_w)
{
    return (point_i_t){
        .x = cx + (int)lrintf(ca * (float)r + nx * (float)half_w),
        .y = cy + (int)lrintf(sa * (float)r + ny * (float)half_w),
    };
}

static void fill_hand_diamond(int cx, int cy, float ca, float sa, int r, int len, int half_w, uint16_t color)
{
    const float nx = -sa;
    const float ny = ca;
    const point_i_t tip = hand_point(cx, cy, ca, sa, nx, ny, r + len, 0);
    const point_i_t right = hand_point(cx, cy, ca, sa, nx, ny, r, half_w);
    const point_i_t tail = hand_point(cx, cy, ca, sa, nx, ny, r - len, 0);
    const point_i_t left = hand_point(cx, cy, ca, sa, nx, ny, r, -half_w);
    fill_triangle_points(tip, right, tail, color);
    fill_triangle_points(tip, tail, left, color);
}

static void fill_hand_spear(int cx, int cy, float ca, float sa, int outer_r, int len, int half_w, uint16_t color)
{
    const float nx = -sa;
    const float ny = ca;
    const point_i_t tip = hand_point(cx, cy, ca, sa, nx, ny, outer_r, 0);
    const point_i_t right = hand_point(cx, cy, ca, sa, nx, ny, outer_r - len, half_w);
    const point_i_t left = hand_point(cx, cy, ca, sa, nx, ny, outer_r - len, -half_w);
    fill_triangle_points(tip, right, left, color);
}

static void draw_ornate_watch_hand(int cx,
                                   int cy,
                                   float angle,
                                   int inner_r,
                                   int outer_r,
                                   uint16_t outline,
                                   uint16_t fill,
                                   int spine_width,
                                   int spear_len,
                                   int spear_half_w,
                                   bool elaborate)
{
    const float ca = cosf(angle);
    const float sa = sinf(angle);
    draw_radial_hand_width(cx, cy, angle, inner_r, outer_r - spear_len + 4, outline, spine_width + 4);
    draw_radial_hand_width(cx, cy, angle, inner_r, outer_r - spear_len + 4, fill, spine_width);
    fill_hand_spear(cx, cy, ca, sa, outer_r, spear_len + 5, spear_half_w + 3, outline);
    fill_hand_spear(cx, cy, ca, sa, outer_r - 2, spear_len, spear_half_w, fill);
    const int span = outer_r - inner_r;
    const int collar_r = inner_r + span * 58 / 100;
    fill_hand_diamond(cx, cy, ca, sa, collar_r, elaborate ? 13 : 10, elaborate ? 8 : 6, outline);
    fill_hand_diamond(cx, cy, ca, sa, collar_r, elaborate ? 10 : 7, elaborate ? 5 : 4, fill);
    const int loop_r = inner_r < -8 ? inner_r : -24;
    const int lx = cx + (int)lrintf(ca * (float)loop_r);
    const int ly = cy + (int)lrintf(sa * (float)loop_r);
    faculty175_display_fill_circle(lx, ly, elaborate ? 8 : 6, outline);
    faculty175_display_fill_circle(lx, ly, elaborate ? 5 : 3, fill);
    faculty175_display_fill_circle(lx, ly, elaborate ? 2 : 1, outline);
    if (elaborate) {
        fill_hand_diamond(cx, cy, ca, sa, inner_r + span * 34 / 100, 8, 5, outline);
        fill_hand_diamond(cx, cy, ca, sa, inner_r + span * 34 / 100, 5, 3, fill);
    }
}

static void draw_ornate_second_hand(int cx, int cy, float angle, uint16_t outline, uint16_t fill)
{
    draw_radial_hand_width(cx, cy, angle, -34, 158, outline, 3);
    draw_radial_hand_width(cx, cy, angle, -30, 158, fill, 1);
    const float ca = cosf(angle);
    const float sa = sinf(angle);
    fill_hand_spear(cx, cy, ca, sa, 166, 14, 3, outline);
    fill_hand_spear(cx, cy, ca, sa, 164, 10, 1, fill);
    const int bx = cx + (int)lrintf(ca * -40.0f);
    const int by = cy + (int)lrintf(sa * -40.0f);
    faculty175_display_fill_circle(bx, by, 5, outline);
    faculty175_display_fill_circle(bx, by, 3, fill);
    faculty175_display_fill_circle(bx, by, 1, outline);
}

static uint8_t blend_u8(uint8_t a, uint8_t b, uint8_t amount)
{
    return (uint8_t)(((uint16_t)a * (uint16_t)(255u - amount) + (uint16_t)b * (uint16_t)amount) / 255u);
}

static uint16_t watch_tint_pixel(uint16_t p, uint8_t tr, uint8_t tg, uint8_t tb, uint8_t tint, uint8_t dim)
{
    uint8_t r = (uint8_t)(((p >> 11) & 0x1f) * 255 / 31);
    uint8_t g = (uint8_t)(((p >> 5) & 0x3f) * 255 / 63);
    uint8_t b = (uint8_t)((p & 0x1f) * 255 / 31);
    const uint8_t maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const uint8_t minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const bool gold_mark = r > 132 && g > 92 && b < 96 && r >= g;
    if (gold_mark) {
        tint = (uint8_t)(tint / 4u);
        dim = (uint8_t)(dim / 2u);
    } else if (maxc > minc + 24u) {
        tint = (uint8_t)((uint16_t)tint * 3u / 4u);
    }
    r = (uint8_t)(((uint16_t)r * (uint16_t)(255u - dim)) / 255u);
    g = (uint8_t)(((uint16_t)g * (uint16_t)(255u - dim)) / 255u);
    b = (uint8_t)(((uint16_t)b * (uint16_t)(255u - dim)) / 255u);
    return rgb565(blend_u8(r, tr, tint), blend_u8(g, tg, tint), blend_u8(b, tb, tint));
}

static void watch_apply_time_of_day_tint(const struct tm *local)
{
    if (s_fb == NULL || local == NULL) {
        return;
    }
    const float hour = (float)local->tm_hour + (float)local->tm_min / 60.0f;
    uint8_t tr = 72;
    uint8_t tg = 118;
    uint8_t tb = 190;
    uint8_t tint = 16;
    uint8_t dim = 0;
    bool dusk_reflection = false;
    if (hour < 5.5f || hour >= 21.0f) {
        tr = 18;
        tg = 42;
        tb = 104;
        tint = 64;
        dim = 58;
    } else if (hour < 8.0f) {
        tr = 246;
        tg = 154;
        tb = 78;
        tint = 44;
        dim = 10;
    } else if (hour < 17.0f) {
        tr = 96;
        tg = 148;
        tb = 220;
        tint = 12;
        dim = 0;
    } else if (hour < 20.0f) {
        tr = 236;
        tg = 122;
        tb = 62;
        tint = 54;
        dim = 16;
        dusk_reflection = true;
    } else {
        tr = 34;
        tg = 62;
        tb = 132;
        tint = 54;
        dim = 36;
    }
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const uint8_t row_tint = (uint8_t)(tint + ((uint16_t)tint * (uint16_t)y) / (uint16_t)(FACULTY175_LCD_H * 4));
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const size_t i = (size_t)y * (size_t)FACULTY175_LCD_W + (size_t)x;
            if (s_fb[i] != 0) {
                s_fb[i] = watch_tint_pixel(s_fb[i], tr, tg, tb, row_tint, dim);
                if (dusk_reflection && y > 64 && y < 270) {
                    const uint16_t p = s_fb[i];
                    const uint8_t r = (uint8_t)(((p >> 11) & 0x1f) * 255 / 31);
                    const uint8_t g = (uint8_t)(((p >> 5) & 0x3f) * 255 / 63);
                    const uint8_t b = (uint8_t)((p & 0x1f) * 255 / 31);
                    if ((uint16_t)r + (uint16_t)g + (uint16_t)b > 170u && r >= b) {
                        s_fb[i] = watch_tint_pixel(p, 255, 142, 64, 30, 0);
                    }
                }
            }
        }
        if ((y & 0x1f) == 0x1f) {
            vTaskDelay(1);
        }
    }
}

static const char *watch_complication_label(faculty175_ui_state_t state, const char *detail)
{
    if (detail != NULL && detail[0] != '\0') {
        return detail;
    }
    switch (state) {
        case FACULTY175_UI_WIFI:
            return "WIFI";
        case FACULTY175_UI_LISTEN:
            return "LISTEN";
        case FACULTY175_UI_CAPTURE:
            return "CAPTURE";
        case FACULTY175_UI_THINK:
            return "THINK";
        case FACULTY175_UI_SPEAK:
            return "SPEAK";
        case FACULTY175_UI_ERROR:
            return "OFFLINE";
        case FACULTY175_UI_BOOT:
        default:
            return "READY";
    }
}

static uint16_t watch_boot_phase_color(uint8_t step, bool lit)
{
    uint8_t r = 90;
    uint8_t g = 72;
    uint8_t b = 44;
    if (step <= 3) {
        r = 70;
        g = 178;
        b = 226;
    } else if (step <= 6) {
        r = 88;
        g = 212;
        b = 146;
    } else if (step <= 9) {
        r = 226;
        g = 170;
        b = 84;
    } else {
        r = 204;
        g = 118;
        b = 224;
    }
    if (!lit) {
        r = (uint8_t)(r / 3u);
        g = (uint8_t)(g / 3u);
        b = (uint8_t)(b / 3u);
    }
    return rgb565(r, g, b);
}

void faculty175_display_draw_pocketwatch(const char *detail, uint32_t anim_ms, bool boot_mode)
{
    const int cx = FACULTY175_PANEL_CX;
    const int cy = FACULTY175_PANEL_CY;
    const float tau = 6.28318530718f;
    const float quarter = 1.57079632679f;
    const uint16_t bg = rgb565(0, 0, 0);
    const uint16_t brass = rgb565(214, 170, 84);
    const uint16_t brass_dim = rgb565(92, 68, 36);
    const uint16_t hand = rgb565(226, 218, 188);
    const uint16_t tick = rgb565(174, 184, 194);
    const uint16_t tick_dim = rgb565(92, 98, 106);
    const uint8_t boot_total = s_boot_watch_total == 0 ? 1 : s_boot_watch_total;
    const uint8_t boot_step = s_boot_watch_step > boot_total ? boot_total : s_boot_watch_step;
    const bool boot_progress = boot_mode || s_boot_watch_progress_active;
    const uint16_t text = rgb565(188, 170, 122);
    const char *label = detail != NULL && detail[0] != '\0' ? detail : "READY";
    if (s_boot_watch_progress_active && s_boot_watch_detail[0] != '\0') {
        label = s_boot_watch_detail;
    }

    const bool custom_bg = faculty175_pocketwatch_draw_background();
    if (!custom_bg) {
        faculty175_display_fill_rgb565(bg);
    }
    const uint16_t second = boot_progress ? rgb565(70, 178, 226) : rgb565(210, 64, 48);

    const int chapter_r = 208;
    const int major_tick_inner = 184;
    const int minor_tick_inner = 195;
    const int hour_hand_r = 88;
    const int minute_hand_r = 142;
    const int second_hand_r = 176;
    const int sub_cy = cy + 96;

    if (!custom_bg) {
        faculty175_display_draw_circle(cx, cy, chapter_r, brass_dim);

        for (int i = 0; i < 60; ++i) {
            const float a = ((float)i / 60.0f) * tau - quarter;
            const bool major = (i % 5) == 0;
            const int r0 = major ? major_tick_inner : minor_tick_inner;
            const int r1 = chapter_r;
            const uint16_t c = major ? tick : tick_dim;
            draw_radial_hand(cx, cy, a, r0, r1, c, major);
        }
    }

    uint32_t tick_second = (anim_ms / 1000u) % 60u;
    float minute_pos = (float)(anim_ms % 3600000u) / 3600000.0f;
    float hour_pos = (float)(anim_ms % 43200000u) / 43200000.0f;
    struct tm local = {};
    const bool local_time_valid = astrolabe_time_valid();
    if (local_time_valid) {
        astrolabe_time_local(&local);
        tick_second = (uint32_t)local.tm_sec;
        minute_pos = ((float)local.tm_min + (float)local.tm_sec / 60.0f) / 60.0f;
        hour_pos = ((float)(local.tm_hour % 12) + (float)local.tm_min / 60.0f +
                    (float)local.tm_sec / 3600.0f) /
                   12.0f;
    }
    const float sec_angle = ((float)tick_second / 60.0f) * tau - quarter;
    const float minute_angle = minute_pos * tau - quarter;
    const float hour_angle = hour_pos * tau - quarter;
    if (custom_bg) {
        if (local_time_valid) {
            watch_apply_time_of_day_tint(&local);
        }
        const uint16_t outline = rgb565(1, 3, 7);
        const uint16_t hour_fill = rgb565(250, 222, 142);
        const uint16_t minute_fill = rgb565(255, 246, 214);
        const uint16_t second_fill = rgb565(228, 192, 96);
        draw_ornate_watch_hand(cx, cy, hour_angle, -24, 104, outline, hour_fill, 8, 28, 12, true);
        draw_ornate_watch_hand(cx, cy, minute_angle, -30, 160, outline, minute_fill, 6, 32, 10, false);
        draw_ornate_second_hand(cx, cy, sec_angle, outline, second_fill);
        faculty175_display_fill_circle(cx, cy, 8, brass);
        faculty175_display_fill_circle(cx, cy, 3, minute_fill);
        faculty175_display_flush();
        return;
    }
    draw_radial_hand(cx, cy, hour_angle, -18, hour_hand_r, hand, true);
    draw_radial_hand(cx, cy, minute_angle, -22, minute_hand_r, hand, true);
    draw_radial_hand(cx, cy, sec_angle, -34, second_hand_r, second, false);
    faculty175_display_fill_circle(cx, cy, 10, brass);
    faculty175_display_fill_circle(cx, cy, 4, hand);

    const int sub_cx = cx;
    faculty175_display_draw_circle(sub_cx, sub_cy, 42, brass_dim);
    faculty175_display_draw_circle(sub_cx, sub_cy, 32, tick_dim);
    if (boot_step > 0) {
        const float gap = 0.025f;
        for (uint8_t i = 0; i < boot_total; ++i) {
            const float a0 = ((float)i / (float)boot_total) * tau - quarter + gap;
            const float a1 = ((float)(i + 1u) / (float)boot_total) * tau - quarter - gap;
            const bool lit = i < boot_step;
            const uint16_t phase = watch_boot_phase_color((uint8_t)(i + 1u), lit);
            draw_bezel_arc(sub_cx, sub_cy, 42, a0, a1, phase);
            draw_bezel_arc(sub_cx, sub_cy, 41, a0, a1, phase);
        }
    }
    for (int i = 0; i < 12; ++i) {
        const float a = ((float)i / 12.0f) * tau - quarter;
        draw_radial_hand(sub_cx, sub_cy, a, 34, 39, i == 0 ? tick : tick_dim, false);
    }
    float sub_angle;
    if (boot_progress) {
        if (!s_boot_watch_progress_active && boot_step >= boot_total) {
            sub_angle = -quarter;
        } else {
            const uint8_t step_index = boot_step == 0 ? 0 : (uint8_t)(boot_step - 1u);
            const float phase_tick = (float)((anim_ms / 1000u) % 4u) / 4.0f;
            const float phase_pos = ((float)step_index + phase_tick) / (float)boot_total;
            sub_angle = phase_pos * tau - quarter;
        }
    } else {
        sub_angle = ((float)tick_second / 60.0f) * tau - quarter;
    }
    draw_radial_hand(sub_cx, sub_cy, sub_angle, 0, 28, second, false);
    draw_text_center_at(label, sub_cx, sub_cy - 4, text);
    faculty175_display_flush();
}

static void draw_watch_status(faculty175_ui_state_t state, const char *detail, uint32_t anim_ms)
{
    faculty175_display_draw_pocketwatch(watch_complication_label(state, detail),
                                        anim_ms,
                                        state == FACULTY175_UI_BOOT || state == FACULTY175_UI_WIFI);
}

static void boot_watch_task(void *arg)
{
    (void)arg;
    while (s_boot_watch_active) {
        char detail[sizeof(s_boot_watch_detail)];
        strncpy(detail, s_boot_watch_detail, sizeof(detail) - 1u);
        detail[sizeof(detail) - 1u] = '\0';
        faculty175_display_lock();
        faculty175_display_draw_pocketwatch(detail,
                                            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                                            true);
        faculty175_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    s_boot_watch_task = NULL;
    vTaskDelete(NULL);
}

static void faculty_display_name(const char *name, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (name == NULL || name[0] == '\0') {
        strncpy(out, "Faculty", cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    if (strlen(name) <= 14) {
        strncpy(out, name, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    const char *sp = strrchr(name, ' ');
    if (sp != NULL && sp[1] != '\0') {
        strncpy(out, sp + 1, cap - 1);
    } else {
        strncpy(out, name, cap - 1);
    }
    out[cap - 1] = '\0';
}

static void faculty_handle_label(const char *slug, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (slug == NULL || slug[0] == '\0') {
        strncpy(out, "a.faculty", cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    if (strncmp(slug, "a.", 2) == 0) {
        strncpy(out, slug, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    if (strncmp(slug, "a-", 2) == 0) {
        snprintf(out, cap, "a.%s", slug + 2);
    } else {
        snprintf(out, cap, "a.%s", slug);
    }
    for (char *p = out; *p != '\0'; ++p) {
        if (*p == '-' || *p == '_') {
            *p = '.';
        }
    }
}

static void draw_pixel_safe(int x, int y, uint16_t color)
{
    if (s_fb == NULL || x < 0 || x >= FACULTY175_LCD_W || y < 0 || y >= FACULTY175_LCD_H) {
        return;
    }
    s_fb[y * FACULTY175_LCD_W + x] = color;
}

static void draw_line_safe(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        draw_pixel_safe(x0, y0, color);
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

void faculty175_display_draw_pixel(int x, int y, uint16_t color)
{
    draw_pixel_safe(x, y, color);
}

void faculty175_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    draw_line_safe(x0, y0, x1, y1, color);
}

void faculty175_display_draw_text(const char *text, int x, int y, uint16_t color)
{
    draw_text(text, x, y, color);
}

void faculty175_display_draw_centered_text(const char *text, int y, uint16_t color)
{
    draw_centered_text(text, y, color);
}

void faculty175_display_touch_visual_update(int16_t x, int16_t y, bool down, uint32_t now_ms)
{
    if (x < 0 || x >= FACULTY175_LCD_W || y < 0 || y >= FACULTY175_LCD_H) {
        return;
    }

    portENTER_CRITICAL(&s_touch_visual_mux);
    if (down && !s_touch_visual_down) {
        s_touch_trail_count = 0;
    }
    if (down) {
        if (s_touch_trail_count > 0) {
            const touch_visual_point_t *last = &s_touch_trail[s_touch_trail_count - 1];
            const int dx = (int)x - (int)last->x;
            const int dy = (int)y - (int)last->y;
            if ((dx * dx + dy * dy) < 9 && now_ms - last->t_ms < 48) {
                s_touch_visual_down = true;
                s_touch_visual_last_ms = now_ms;
                portEXIT_CRITICAL(&s_touch_visual_mux);
                return;
            }
        }
        if (s_touch_trail_count >= FACULTY175_TOUCH_TRAIL_LEN) {
            memmove(&s_touch_trail[0], &s_touch_trail[1],
                    sizeof(s_touch_trail[0]) * (FACULTY175_TOUCH_TRAIL_LEN - 1));
            s_touch_trail_count = FACULTY175_TOUCH_TRAIL_LEN - 1;
        }
        s_touch_trail[s_touch_trail_count++] = (touch_visual_point_t){
            .x = x,
            .y = y,
            .t_ms = now_ms,
        };
    }
    s_touch_visual_down = down;
    s_touch_visual_last_ms = now_ms;
    portEXIT_CRITICAL(&s_touch_visual_mux);
}

void faculty175_display_nav_mode_set(bool enabled)
{
    s_nav_mode = enabled;
}

void faculty175_display_flush_suspended_set(bool suspended)
{
    s_flush_suspended = suspended;
}

void faculty175_display_flush(void)
{
    if (s_flush_suspended) {
        return;
    }
    draw_bezel_nav();
    draw_stored_bezel_waveform();
    draw_touch_visual();
    faculty175_display_flush_fb();
}

void faculty175_display_flush_rect(int x, int y, int w, int h)
{
    if (s_flush_suspended || s_panel == NULL || s_fb == NULL || s_flush_done == NULL || w <= 0 || h <= 0) {
        return;
    }
    if (!faculty175_flush_strip_alloc()) {
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
    if (x >= FACULTY175_LCD_W || y >= FACULTY175_LCD_H || w <= 0 || h <= 0) {
        return;
    }
    if (x + w > FACULTY175_LCD_W) {
        w = FACULTY175_LCD_W - x;
    }
    if (y + h > FACULTY175_LCD_H) {
        h = FACULTY175_LCD_H - y;
    }

    const int max_strip_h = s_flush_strip_h > 0 ? s_flush_strip_h : FACULTY175_LCD_FLUSH_STRIP_H;
    for (int row0 = 0; row0 < h; row0 += max_strip_h) {
        int strip_h = h - row0;
        if (strip_h > max_strip_h) {
            strip_h = max_strip_h;
        }
        for (int row = 0; row < strip_h; ++row) {
            const uint16_t *src = &s_fb[(y + row0 + row) * FACULTY175_LCD_W + x];
            uint16_t *dst = &s_flush_strip[row * w];
            for (int col = 0; col < w; ++col) {
                dst[col] = rgb565_panel_wire(src[col]);
            }
        }
        (void)xSemaphoreTake(s_flush_done, 0);
        if (esp_lcd_panel_draw_bitmap(s_panel, x, y + row0, x + w, y + row0 + strip_h, s_flush_strip) != ESP_OK) {
            ESP_LOGW(TAG, "lcd flush rect %d,%d %dx%d failed", x, y + row0, w, strip_h);
            break;
        }
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(20)) != pdTRUE) {
            ESP_LOGW(TAG, "lcd flush rect %d,%d %dx%d timeout", x, y + row0, w, strip_h);
        }
    }
}

void faculty175_display_draw_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    int x = r;
    int y = 0;
    int err = 1 - x;
    while (x >= y) {
        draw_pixel_safe(cx + x, cy + y, color);
        draw_pixel_safe(cx + y, cy + x, color);
        draw_pixel_safe(cx - y, cy + x, color);
        draw_pixel_safe(cx - x, cy + y, color);
        draw_pixel_safe(cx - x, cy - y, color);
        draw_pixel_safe(cx - y, cy - x, color);
        draw_pixel_safe(cx + y, cy - x, color);
        draw_pixel_safe(cx + x, cy - y, color);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x + 1);
        }
    }
}

void faculty175_display_fill_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= r * r) {
                draw_pixel_safe(cx + x, cy + y, color);
            }
        }
        if (((y + r) & 0x0f) == 0x0f) {
            vTaskDelay(1);
        }
    }
}

static void draw_bezel_ring(int cx, int cy, int outer_r, int inner_r, uint16_t color)
{
    (void)inner_r;
    if (s_fb == NULL || outer_r <= 0) {
        return;
    }

    int x = outer_r;
    int y = 0;
    int err = 1 - x;
    while (x >= y) {
        draw_pixel_safe(cx + x, cy + y, color);
        draw_pixel_safe(cx + y, cy + x, color);
        draw_pixel_safe(cx - y, cy + x, color);
        draw_pixel_safe(cx - x, cy + y, color);
        draw_pixel_safe(cx - x, cy - y, color);
        draw_pixel_safe(cx - y, cy - x, color);
        draw_pixel_safe(cx + y, cy - x, color);
        draw_pixel_safe(cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static void draw_bezel_arc(int cx, int cy, int r, float start, float end, uint16_t color)
{
    if (r <= 0 || end <= start) {
        return;
    }

    const int steps = 24;
    int prev_x = cx + (int)lroundf((float)r * cosf(start));
    int prev_y = cy + (int)lroundf((float)r * sinf(start));
    for (int i = 1; i <= steps; ++i) {
        const float t = start + (end - start) * (float)i / (float)steps;
        const int x = cx + (int)lroundf((float)r * cosf(t));
        const int y = cy + (int)lroundf((float)r * sinf(t));
        draw_line_safe(prev_x, prev_y, x, y, color);
        prev_x = x;
        prev_y = y;
    }
}

static bool face_supports_vertical_nav(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_FACULTY:
        case FACULTY175_FACE_SYNASTRY:
            return true;
        default:
            return false;
    }
}

static void draw_bezel_nav(void)
{
    size_t index = 0;
    size_t count = 0;
    if (!faculty175_faces_nav_position(&index, &count) || count <= 1) {
        return;
    }

    const int cx = FACULTY175_PANEL_CX;
    const int cy = FACULTY175_PANEL_CY;
    const int r = FACULTY175_BEZEL_OUTER_R;
    const uint16_t base = rgb565(38, 44, 58);
    const uint16_t active = s_nav_mode ? rgb565(78, 204, 224) : rgb565(255, 214, 112);
    const float full = 2.0f * (float)M_PI;
    const float step = full / (float)count;
    const float start0 = -0.5f * (float)M_PI - (step * 0.5f);

    if (s_nav_mode) {
        const float pad = step > 0.09f ? 0.028f : 0.01f;
        const float active_start = start0 + (float)index * step + pad;
        const float active_end = start0 + (float)(index + 1) * step - pad;
        draw_bezel_arc(cx, cy, r, active_start, active_end, active);
        draw_bezel_arc(cx, cy, r - 1, active_start, active_end, active);
        return;
    } else {
        draw_bezel_arc(cx, cy, r, 0.0f, full, base);
    }

    const float pad = step > 0.09f ? 0.018f : 0.006f;
    const float active_start = start0 + (float)index * step + pad;
    const float active_end = start0 + (float)(index + 1) * step - pad;
    const faculty175_face_desc_t *face = faculty175_faces_current();
    const bool vertical_nav = s_nav_mode || (face != NULL && face_supports_vertical_nav(face->id));
    if (!s_nav_mode && vertical_nav) {
        const float nav_pad = step > 0.09f ? 0.034f : 0.012f;
        draw_bezel_arc(cx, cy, r + 1, active_start + nav_pad, active_end - nav_pad, rgb565(84, 224, 255));
        draw_bezel_arc(cx, cy, r - 1, active_start + nav_pad, active_end - nav_pad, rgb565(178, 150, 255));
    }
    if (!s_nav_mode) {
        draw_bezel_arc(cx, cy, r, active_start, active_end, active);
    }

    if (face != NULL && face->label != NULL && face->label[0] != '\0') {
        faculty175_display_draw_bezel_label(face->label, false, FACULTY175_NAME_ARC_R, 0, rgb565(210, 218, 238));
    }
}

static uint16_t touch_visual_color(uint32_t age_ms, bool hot)
{
    if (hot) {
        return rgb565(255, 220, 120);
    }
    if (age_ms < 140) {
        return rgb565(92, 232, 255);
    }
    if (age_ms < 340) {
        return rgb565(70, 172, 220);
    }
    if (age_ms < 540) {
        return rgb565(54, 108, 168);
    }
    return rgb565(38, 66, 112);
}

static void draw_touch_visual(void)
{
    touch_visual_point_t points[FACULTY175_TOUCH_TRAIL_LEN];
    uint8_t count = 0;
    bool down = false;
    uint32_t last_ms = 0;

    portENTER_CRITICAL(&s_touch_visual_mux);
    count = s_touch_trail_count;
    if (count > FACULTY175_TOUCH_TRAIL_LEN) {
        count = FACULTY175_TOUCH_TRAIL_LEN;
    }
    memcpy(points, s_touch_trail, sizeof(points[0]) * count);
    down = s_touch_visual_down;
    last_ms = s_touch_visual_last_ms;
    portEXIT_CRITICAL(&s_touch_visual_mux);

    if (count == 0) {
        return;
    }

    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    const uint32_t since_last = now_ms - last_ms;
    if (!down && since_last > FACULTY175_TOUCH_TRAIL_MS) {
        return;
    }

    for (uint8_t i = 1; i < count; ++i) {
        const uint32_t age = now_ms - points[i].t_ms;
        if (age > FACULTY175_TOUCH_TRAIL_MS) {
            continue;
        }
        const uint16_t color = touch_visual_color(age, false);
        draw_line_safe(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, color);
        if (age < 260) {
            draw_line_safe(points[i - 1].x + 1, points[i - 1].y, points[i].x + 1, points[i].y, color);
            draw_line_safe(points[i - 1].x, points[i - 1].y + 1, points[i].x, points[i].y + 1, color);
        }
    }

    const touch_visual_point_t *tip = &points[count - 1];
    const uint32_t tip_age = now_ms - tip->t_ms;
    if (tip_age > FACULTY175_TOUCH_TRAIL_MS) {
        return;
    }
    const int pulse = down ? (int)((now_ms / 70u) % 5u) : (int)(tip_age / 90u);
    const int r = down ? 8 + pulse : 10 + pulse;
    const uint16_t ring = touch_visual_color(tip_age, down);
    const uint16_t core = down ? rgb565(255, 246, 198) : touch_visual_color(tip_age, false);
    faculty175_display_draw_circle(tip->x, tip->y, r, ring);
    faculty175_display_draw_circle(tip->x, tip->y, r + 4, touch_visual_color(tip_age + 180, false));
    faculty175_display_fill_circle(tip->x, tip->y, down ? 3 : 2, core);
}

static uint16_t bezel_rgb(uint8_t r, uint8_t g, uint8_t b, void *user)
{
    (void)user;
    return rgb565(r, g, b);
}

static void bezel_line(int x0, int y0, int x1, int y1, uint16_t color, void *user)
{
    (void)user;
    draw_line_safe(x0, y0, x1, y1, color);
}

static void bezel_circle(int cx, int cy, int r, uint16_t color, void *user)
{
    (void)user;
    faculty175_display_draw_circle(cx, cy, r, color);
}

static void bezel_fill_circle(int cx, int cy, int r, uint16_t color, void *user)
{
    (void)user;
    faculty175_display_fill_circle(cx, cy, r, color);
}

static void bezel_text(const char *text, int x, int y, uint16_t color, void *user)
{
    (void)user;
    if (text != NULL && text[0] != '\0') {
        draw_char5x7(text[0], x, y, color);
    }
}

void faculty175_display_draw_bezel_label(const char *text, bool bottom, int radius, uint32_t scroll_ms, uint16_t color)
{
    const astrolabe_round_bezel_ops_t ops = {
        .width = FACULTY175_LCD_W,
        .height = FACULTY175_LCD_H,
        .cx = FACULTY175_PANEL_CX,
        .cy = FACULTY175_PANEL_CY,
        .outer_radius = FACULTY175_BEZEL_OUTER_R,
        .inner_radius = FACULTY175_BEZEL_INNER_R,
        .rgb565 = bezel_rgb,
        .draw_line = bezel_line,
        .draw_circle = bezel_circle,
        .fill_circle = bezel_fill_circle,
        .draw_text = bezel_text,
    };
    const astrolabe_round_bezel_label_t label = {
        .text = text,
        .radius = radius > 0 ? radius : FACULTY175_NAME_ARC_R,
        .color = color,
        .position = bottom ? ASTROLABE_BEZEL_LABEL_BOTTOM : ASTROLABE_BEZEL_LABEL_TOP,
        .scroll_ms = scroll_ms,
    };
    astrolabe_round_bezel_draw_label(&ops, &label);
}

static void bust_initials(const char *name, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (name == NULL || name[0] == '\0') {
        strncpy(out, "?", cap - 1);
        return;
    }
    out[0] = (char)toupper((unsigned char)name[0]);
    size_t n = 1;
    const char *sp = strrchr(name, ' ');
    if (sp != NULL && sp[1] != '\0' && n + 1 < cap) {
        out[n++] = (char)toupper((unsigned char)sp[1]);
    } else if (name[1] != '\0' && n + 1 < cap) {
        out[n++] = (char)toupper((unsigned char)name[1]);
    }
    out[n] = '\0';
}

static void draw_bezel_waveform(const uint8_t *samples,
                                const uint8_t *stream_mask,
                                size_t count,
                                uint16_t color_idle,
                                uint16_t color_stream)
{
    if (samples == NULL || count == 0) {
        return;
    }

    const int cx = FACULTY175_PANEL_CX;
    const int cy = FACULTY175_PANEL_CY;
    const int base_r = FACULTY175_BEZEL_OUTER_R;
    const float start = -0.5f * (float)M_PI;
    const float full = 2.0f * (float)M_PI;
    const int steps = 224;
    int prev_x = 0;
    int prev_y = 0;
    bool have_prev = false;

    for (int i = 0; i < steps; ++i) {
        const size_t forward_si = (count <= 1) ? 0 : (size_t)i * (count - 1) / (size_t)(steps - 1);
        const size_t si = (count <= 1) ? 0 : (count - 1) - forward_si;
        const uint8_t v = samples[si];
        const int amp = ((int)v * 9) / 255;
        const int r = base_r - 1 + amp;
        const float t = start + full * (float)i / (float)(steps - 1);
        const int px = cx + (int)lroundf((float)r * cosf(t));
        const int py = cy + (int)lroundf((float)r * sinf(t));
        const bool streamed = stream_mask != NULL && stream_mask[si] > 0;
        const uint8_t lift = (uint8_t)(92u + ((uint16_t)v * 163u) / 255u);
        const uint16_t color = streamed
            ? mix_rgb565_rgb888(54, 26, 16, (uint8_t)(color_stream >> 11 << 3),
                                 (uint8_t)(((color_stream >> 5) & 0x3f) << 2),
                                 (uint8_t)((color_stream & 0x1f) << 3), lift)
            : mix_rgb565_rgb888(8, 38, 30, (uint8_t)(color_idle >> 11 << 3),
                                 (uint8_t)(((color_idle >> 5) & 0x3f) << 2),
                                 (uint8_t)((color_idle & 0x1f) << 3), lift);
        if (have_prev) {
            draw_line_safe(prev_x, prev_y, px, py, color);
            if (v > 120) {
                const int inner_r = base_r - 3;
                const int ix = cx + (int)lroundf((float)inner_r * cosf(t));
                const int iy = cy + (int)lroundf((float)inner_r * sinf(t));
                draw_pixel_safe(ix, iy, color);
            }
        } else {
            draw_pixel_safe(px, py, color);
        }
        prev_x = px;
        prev_y = py;
        have_prev = true;
    }
}

void faculty175_display_waveform_update(const uint8_t *waveform,
                                        const uint8_t *waveform_stream,
                                        size_t waveform_len,
                                        bool visible)
{
    size_t n = waveform_len;
    if (n > FACULTY175_BEZEL_WAVEFORM_MAX) {
        n = FACULTY175_BEZEL_WAVEFORM_MAX;
    }

    portENTER_CRITICAL(&s_waveform_visual_mux);
    s_bezel_waveform_visible = visible && waveform != NULL && n > 0;
    s_bezel_waveform_len = s_bezel_waveform_visible ? n : 0;
    if (s_bezel_waveform_visible) {
        memcpy(s_bezel_waveform, waveform, n);
        if (waveform_stream != NULL) {
            memcpy(s_bezel_waveform_stream, waveform_stream, n);
        } else {
            memset(s_bezel_waveform_stream, 0, n);
        }
    }
    portEXIT_CRITICAL(&s_waveform_visual_mux);
}

static void draw_stored_bezel_waveform(void)
{
    uint8_t waveform[FACULTY175_BEZEL_WAVEFORM_MAX];
    uint8_t stream[FACULTY175_BEZEL_WAVEFORM_MAX];
    size_t len = 0;
    bool visible = false;

    portENTER_CRITICAL(&s_waveform_visual_mux);
    visible = s_bezel_waveform_visible;
    len = s_bezel_waveform_len;
    if (len > FACULTY175_BEZEL_WAVEFORM_MAX) {
        len = FACULTY175_BEZEL_WAVEFORM_MAX;
    }
    if (visible && len > 0) {
        memcpy(waveform, s_bezel_waveform, len);
        memcpy(stream, s_bezel_waveform_stream, len);
    }
    portEXIT_CRITICAL(&s_waveform_visual_mux);

    if (!visible || len == 0) {
        return;
    }
    draw_bezel_waveform(waveform, stream, len, rgb565(48, 210, 140), rgb565(255, 170, 90));
}

static void draw_faculty_portrait(const char *faculty_name,
                                  const char *faculty_slug,
                                  int x,
                                  int y,
                                  int w,
                                  int h,
                                  uint16_t accent)
{
    const int bust_side = FACULTY175_FACULTY_BUST_W;
    const int bx = x + (w - bust_side) / 2;
    const int by = y + (h - bust_side) / 2;

    int bust_x = bx;
    int bust_y = by;
    faculty175_faculty_bust_blit_origin(bx, by, bust_side, bust_side, false, &bust_x, &bust_y);
    if (!faculty175_faculty_draw_bust(bust_x, bust_y)) {
        char initials[4];
        bust_initials(faculty_name, initials, sizeof(initials));
        const int text_w = (int)strlen(initials) * 6;
        const int text_x = bx + (bust_side - text_w) / 2;
        draw_text(initials, text_x, by + bust_side / 2 - 3, rgb565(230, 235, 245));

        if (faculty175_faculty_bust_status() == FACULTY175_FACULTY_BUST_LOADING) {
            draw_text("load", bx + bust_side / 2 - 12, by + bust_side - 16, rgb565(120, 130, 150));
        }
    }

    draw_bezel_ring(FACULTY175_PANEL_CX, FACULTY175_PANEL_CY, FACULTY175_BEZEL_OUTER_R, FACULTY175_BEZEL_INNER_R,
                    accent);

    char label[32];
    faculty_display_name(faculty_name, label, sizeof(label));
    faculty175_display_draw_bezel_label(label, false, FACULTY175_NAME_ARC_R, 0, accent);
}

static void draw_faculty_bottom_label(const char *faculty_slug)
{
    char handle[32];
    faculty_handle_label(faculty_slug, handle, sizeof(handle));
    faculty175_display_draw_bezel_label(handle, true, FACULTY175_NAME_ARC_R, 0, rgb565(214, 224, 246));
}

void faculty175_display_draw_status(faculty175_ui_state_t state,
                                    const char *faculty_name,
                                    const char *detail,
                                    uint32_t anim_ms,
                                    const uint8_t *waveform,
                                    const uint8_t *waveform_stream,
                                    size_t waveform_len)
{
    if (state == FACULTY175_UI_BOOT || state == FACULTY175_UI_WIFI || state == FACULTY175_UI_ERROR ||
        state == FACULTY175_UI_THINK || state == FACULTY175_UI_SPEAK) {
        (void)faculty_name;
        (void)waveform;
        (void)waveform_stream;
        (void)waveform_len;
        draw_watch_status(state, detail, anim_ms);
        return;
    }

    const uint16_t bg = faculty175_ui_bg565();
    faculty175_display_fill_rgb565(bg);

    uint16_t ring = rgb565(40, 245, 168);
    switch (state) {
        case FACULTY175_UI_BOOT:
            ring = rgb565(80, 120, 180);
            break;
        case FACULTY175_UI_WIFI:
            ring = rgb565(255, 184, 77);
            break;
        case FACULTY175_UI_LISTEN:
            ring = rgb565(40, 245, 168);
            break;
        case FACULTY175_UI_CAPTURE:
            ring = rgb565(255, 120, 80);
            break;
        case FACULTY175_UI_THINK:
            ring = rgb565(255, 184, 77);
            break;
        case FACULTY175_UI_SPEAK:
            ring = rgb565(120, 180, 255);
            break;
        case FACULTY175_UI_ERROR:
            ring = rgb565(255, 60, 60);
            break;
    }
    (void)anim_ms;

    faculty175_faculty_roster_entry_t active_faculty = {};
    const char *faculty_slug = "";
    if (faculty175_faculty_roster_active(&active_faculty)) {
        faculty_slug = active_faculty.slug;
    }
    draw_faculty_portrait(faculty_name, faculty_slug, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H, ring);

    draw_bezel_nav();
    if ((state == FACULTY175_UI_LISTEN || state == FACULTY175_UI_CAPTURE) && waveform != NULL && waveform_len > 0) {
        const uint16_t wave_idle = rgb565(48, 210, 140);
        const uint16_t wave_stream = rgb565(255, 170, 90);
        draw_bezel_waveform(waveform, waveform_stream, waveform_len, wave_idle, wave_stream);
    }
    draw_faculty_bottom_label(faculty_slug);

    draw_touch_visual();
    if (s_panel != NULL && s_fb != NULL) {
        faculty175_display_flush_fb();
    }
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

size_t faculty175_display_bmp_size(void)
{
    const uint32_t row_stride = ((FACULTY175_LCD_W * 24u + 31u) / 32u) * 4u;
    return 54u + row_stride * (uint32_t)FACULTY175_LCD_H;
}

size_t faculty175_display_bmp565_size(void)
{
    const uint32_t row_stride = ((FACULTY175_LCD_W * 16u + 31u) / 32u) * 4u;
    return 70u + row_stride * (uint32_t)FACULTY175_LCD_H;
}

esp_err_t faculty175_display_write_bmp565(faculty175_display_write_cb_t write_cb, void *ctx)
{
    if (s_fb == NULL || write_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    enum {
        HEADER_BYTES = 70,
        ROWS_PER_CHUNK = 4,
    };
    const int w = FACULTY175_LCD_W;
    const int h = FACULTY175_LCD_H;
    const uint32_t row_stride = (((uint32_t)w * 16u + 31u) / 32u) * 4u;
    const uint32_t pixel_bytes = row_stride * (uint32_t)h;
    const uint32_t file_size = HEADER_BYTES + pixel_bytes;
    uint8_t header[HEADER_BYTES] = {};

    header[0] = 'B';
    header[1] = 'M';
    put_le32(header + 2, file_size);
    put_le32(header + 10, HEADER_BYTES);
    put_le32(header + 14, 40u);
    put_le32(header + 18, (uint32_t)w);
    put_le32(header + 22, (uint32_t)h);
    put_le16(header + 26, 1u);
    put_le16(header + 28, 16u);
    put_le32(header + 30, 3u);
    put_le32(header + 34, pixel_bytes);
    put_le32(header + 54, 0x0000F800u);
    put_le32(header + 58, 0x000007E0u);
    put_le32(header + 62, 0x0000001Fu);
    put_le32(header + 66, 0x00000000u);

    esp_err_t err = write_cb(ctx, header, sizeof(header));
    if (err != ESP_OK) {
        return err;
    }

    const size_t chunk_bytes = (size_t)row_stride * (size_t)ROWS_PER_CHUNK;
    uint8_t *chunk = heap_caps_malloc(chunk_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (chunk == NULL) {
        chunk = malloc(chunk_bytes);
    }
    if (chunk == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int yi = 0; yi < h;) {
        const int rows = (h - yi) > ROWS_PER_CHUNK ? ROWS_PER_CHUNK : (h - yi);
        uint8_t *out = chunk;
        for (int r = 0; r < rows; ++r, ++yi) {
            const int sy = h - 1 - yi;
            const uint16_t *src = &s_fb[sy * w];
            uint8_t *dst = out;
            for (int sx = 0; sx < w; ++sx) {
                const uint16_t c = src[sx];
                *dst++ = (uint8_t)(c & 0xFFu);
                *dst++ = (uint8_t)(c >> 8);
            }
            while ((uint32_t)(dst - out) < row_stride) {
                *dst++ = 0;
            }
            out += row_stride;
        }
        err = write_cb(ctx, chunk, (size_t)rows * (size_t)row_stride);
        if (err != ESP_OK) {
            free(chunk);
            return err;
        }
        vTaskDelay(0);
    }

    free(chunk);
    return ESP_OK;
}

int faculty175_display_write_bmp(FILE *out)
{
    if (s_fb == NULL || out == NULL) {
        return -1;
    }

    const int w = FACULTY175_LCD_W;
    const int h = FACULTY175_LCD_H;
    const uint32_t row_stride = (((uint32_t)w * 24u + 31u) / 32u) * 4u;
    const uint32_t pixel_bytes = row_stride * (uint32_t)h;
    const uint32_t file_size = 54u + pixel_bytes;

    uint8_t *buf = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = (uint8_t *)malloc(file_size);
    }
    if (buf == NULL) {
        return -1;
    }

    memset(buf, 0, file_size);
    buf[0] = 'B';
    buf[1] = 'M';
    put_le32(buf + 2, file_size);
    put_le32(buf + 10, 54u);
    put_le32(buf + 14, 40u);
    put_le32(buf + 18, (uint32_t)w);
    put_le32(buf + 22, (uint32_t)h);
    put_le16(buf + 26, 1u);
    put_le16(buf + 28, 24u);
    put_le32(buf + 34, pixel_bytes);

    uint8_t *pix = buf + 54;
    for (int yi = 0; yi < h; ++yi) {
        const int sy = h - 1 - yi;
        uint8_t *dst = pix + (uint32_t)yi * row_stride;
        for (int sx = 0; sx < w; ++sx) {
            const uint16_t c = s_fb[sy * w + sx];
            const unsigned r5 = (c >> 11) & 0x1Fu;
            const unsigned g6 = (c >> 5) & 0x3Fu;
            const unsigned b5 = c & 0x1Fu;
            *dst++ = (uint8_t)((b5 * 255u + 15u) / 31u);
            *dst++ = (uint8_t)((g6 * 255u + 31u) / 63u);
            *dst++ = (uint8_t)((r5 * 255u + 15u) / 31u);
        }
        for (uint32_t pad = (uint32_t)w * 3u; pad < row_stride; ++pad) {
            *dst++ = 0;
        }
        if ((yi & 0x1f) == 0x1f) {
            vTaskDelay(0);
        }
    }

    size_t wrote = 0;
    while (wrote < file_size) {
        const size_t chunk = (file_size - wrote) > 4096u ? 4096u : (file_size - wrote);
        const size_t n = fwrite(buf + wrote, 1, chunk, out);
        if (n == 0) {
            break;
        }
        wrote += n;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    free(buf);
    return wrote == file_size ? (int)file_size : -1;
}

bool faculty175_button_pressed(void)
{
    return gpio_get_level(FACULTY175_BUTTON_GPIO) == 0;
}

bool faculty175_button_just_pressed(void)
{
    if (s_button_injected_presses > 0) {
        --s_button_injected_presses;
        return true;
    }
    const bool now = faculty175_button_pressed();
    const bool edge = now && !s_button_prev;
    s_button_prev = now;
    return edge;
}

void faculty175_button_inject_press(void)
{
    ++s_button_injected_presses;
}
