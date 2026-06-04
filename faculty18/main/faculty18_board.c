#include "faculty18_board.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "es8311.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "atom_faculty.h"
#include "faculty18_pmu.h"

static const char *TAG = "faculty_board";

/* Waveshare ESP32-S3-Touch-AMOLED-1.8 — vendor/ESP32-S3-Touch-AMOLED-1.8 examples. */
#define FACULTY18_LCD_HOST SPI2_HOST
#define FACULTY18_LCD_PIN_CS GPIO_NUM_12
#define FACULTY18_LCD_PIN_PCLK GPIO_NUM_11
#define FACULTY18_LCD_PIN_DATA0 GPIO_NUM_4
#define FACULTY18_LCD_PIN_DATA1 GPIO_NUM_5
#define FACULTY18_LCD_PIN_DATA2 GPIO_NUM_6
#define FACULTY18_LCD_PIN_DATA3 GPIO_NUM_7

#define ATOM_AUDIO_I2C_SDA GPIO_NUM_15
#define ATOM_AUDIO_I2C_SCL GPIO_NUM_14
#define ATOM_I2S_MCLK GPIO_NUM_16
#define ATOM_I2S_BCK GPIO_NUM_9
#define ATOM_I2S_WS GPIO_NUM_45
#define ATOM_I2S_DOUT GPIO_NUM_8
#define ATOM_I2S_DIN GPIO_NUM_10
#define ATOM_PA_GPIO GPIO_NUM_46
#define ATOM_ES8311_ADDR 0x18
#define ATOM_I2S_MCLK_MULTIPLE 384

#define ATOM_TCA9554_ADDR 0x20

#define ATOM_I2C_PORT I2C_NUM_0

#define ATOM_BUTTON_GPIO GPIO_NUM_0

#define ATOM_PANEL_GAP_X 0
#define ATOM_PANEL_GAP_Y 0

#define FACULTY18_UI_BG_R 0
#define FACULTY18_UI_BG_G 0
#define FACULTY18_UI_BG_B 0

static bool s_audio_ready;
static bool s_tca9554_ok;
static es8311_handle_t s_es8311;
static int32_t s_mic_probe_peak;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_fb;
static bool s_button_prev;
static uint16_t *s_flush_strip;

/** SH8601 QSPI expects RGB565 high byte first on the wire. */
static uint16_t rgb565_panel_wire(uint16_t logical565)
{
    return (uint16_t)((logical565 >> 8) | (logical565 << 8));
}

/** Map panel pixel (px,py) to logical framebuffer after 90° CCW rotation. */
static uint16_t fb_sample_rotated_ccw(int px, int py)
{
    const int src_x = FACULTY18_LCD_W - 1 - py;
    const int src_y = px;
    if (src_x < 0 || src_x >= FACULTY18_LCD_W || src_y < 0 || src_y >= FACULTY18_LCD_H) {
        return rgb565_panel_wire(0);
    }
    return rgb565_panel_wire(s_fb[src_y * FACULTY18_LCD_W + src_x]);
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
    return false;
}

static void faculty18_display_flush_fb(void)
{
    if (s_panel == NULL || s_fb == NULL || s_flush_done == NULL) {
        return;
    }

    const int strip_h = 32;
    const size_t strip_bytes = (size_t)FACULTY18_LCD_W * (size_t)strip_h * sizeof(uint16_t);

    if (s_flush_strip == NULL) {
        s_flush_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_flush_strip == NULL) {
            s_flush_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (s_flush_strip == NULL) {
        return;
    }

    for (int y = 0; y < FACULTY18_LCD_H; y += strip_h) {
        int h = strip_h;
        if (y + h > FACULTY18_LCD_H) {
            h = FACULTY18_LCD_H - y;
        }
        for (int row = 0; row < h; ++row) {
            const int py = y + row;
            for (int px = 0; px < FACULTY18_LCD_W; ++px) {
                s_flush_strip[row * FACULTY18_LCD_W + px] = fb_sample_rotated_ccw(px, py);
            }
        }
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, FACULTY18_LCD_W, y + h, s_flush_strip) != ESP_OK) {
            ESP_LOGW(TAG, "lcd flush strip y=%d failed", y);
            break;
        }
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "lcd flush strip y=%d timeout", y);
            break;
        }
    }
}

static uint16_t lcd_pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/** SH8601 AMOLED — RGB565 matches the framebuffer directly. */
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

static uint16_t atom_ui_bg565(void)
{
    return rgb565(FACULTY18_UI_BG_R, FACULTY18_UI_BG_G, FACULTY18_UI_BG_B);
}

uint16_t faculty18_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint16_t faculty18_display_fb_from_logical565(uint16_t logical565)
{
    return logical565;
}

uint16_t faculty18_display_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint32_t faculty18_display_bkgd_u32(void)
{
    return ((uint32_t)FACULTY18_UI_BG_B << 16) | ((uint32_t)FACULTY18_UI_BG_G << 8) | FACULTY18_UI_BG_R;
}

static esp_err_t tca9554_write_reg(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(ATOM_I2C_PORT, ATOM_TCA9554_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(1000));
}

static esp_err_t atom_i2c_init(void)
{
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = ATOM_AUDIO_I2C_SDA,
        .scl_io_num = ATOM_AUDIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(ATOM_I2C_PORT, &cfg), TAG, "i2c cfg");
    ESP_RETURN_ON_ERROR(i2c_driver_install(ATOM_I2C_PORT, cfg.mode, 0, 0, 0), TAG, "i2c install");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

/** TCA9554 display power — Waveshare 05_LVGL_WITH_RAM (pins 0–2 rail enable). */
static esp_err_t tca9554_power_on(void)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (tca9554_write_reg(0x03, 0xF8) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (tca9554_write_reg(0x01, 0x00) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        if (tca9554_write_reg(0x01, 0x07) == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_FAIL;
}

static int32_t sample_abs(int16_t s)
{
    return s < 0 ? -(int32_t)s : (int32_t)s;
}

/** M5 Echo Base records stereo interleaved 16-bit on I2S; pick the louder slot. */
static esp_err_t atom_i2s_read_mono(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    if (s_i2s_rx == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int16_t *stereo = heap_caps_malloc(sample_count * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (stereo == NULL) {
        stereo = malloc(sample_count * 2 * sizeof(int16_t));
    }
    if (stereo == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const size_t bytes_want = sample_count * 2 * sizeof(int16_t);
    size_t bytes_read = 0;
    const esp_err_t err =
        i2s_channel_read(s_i2s_rx, stereo, bytes_want, &bytes_read, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK || bytes_read < sizeof(int16_t) * 2) {
        free(stereo);
        return ESP_FAIL;
    }

    const size_t stereo_samples = bytes_read / sizeof(int16_t);
    const size_t frames = stereo_samples / 2;
    const size_t out_frames = frames < sample_count ? frames : sample_count;
    for (size_t i = 0; i < out_frames; ++i) {
        const int32_t left = stereo[i * 2];
        const int32_t right = stereo[i * 2 + 1];
        samples[i] = (int16_t)(sample_abs((int16_t)right) > sample_abs((int16_t)left) ? right : left);
    }
    free(stereo);

    if (out_read != NULL) {
        *out_read = out_frames;
    }
    return ESP_OK;
}

static int32_t faculty18_audio_probe_peak(void)
{
    int16_t frame[320];
    int32_t peak = 0;

    vTaskDelay(pdMS_TO_TICKS(80));
    for (int i = 0; i < 8; ++i) {
        size_t got = 0;
        if (atom_i2s_read_mono(frame, 320, &got, 200) != ESP_OK || got == 0) {
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

/** ES8311 mic path for Waveshare onboard codec (06_I2SCodec example). */
static esp_err_t faculty18_es8311_init(void)
{
    s_es8311 = es8311_create(ATOM_I2C_PORT, ATOM_ES8311_ADDR);
    ESP_RETURN_ON_FALSE(s_es8311, ESP_FAIL, TAG, "es8311 create");

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = FACULTY18_AUDIO_RATE * ATOM_I2S_MCLK_MULTIPLE,
        .sample_frequency = FACULTY18_AUDIO_RATE,
    };
    ESP_RETURN_ON_ERROR(es8311_init(s_es8311, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "es8311 init");
    ESP_RETURN_ON_ERROR(
        es8311_sample_frequency_config(s_es8311, clk.mclk_frequency, clk.sample_frequency), TAG, "es8311 sf");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es8311, 70, NULL), TAG, "es8311 vol");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_es8311, false), TAG, "es8311 mic cfg");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(s_es8311, ES8311_MIC_GAIN_42DB), TAG, "es8311 mic gain");
    return ESP_OK;
}

static esp_err_t faculty18_i2s_set_rate(uint32_t hz)
{
    if (s_i2s_tx == NULL || s_i2s_rx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_tx), TAG, "i2s tx dis");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_rx), TAG, "i2s rx dis");

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    clk_cfg.mclk_multiple = ATOM_I2S_MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg), TAG, "i2s tx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_rx, &clk_cfg), TAG, "i2s rx clk");

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "i2s rx en");
    return ESP_OK;
}

static const sh8601_lcd_init_cmd_t s_sh8601_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static esp_err_t atom_lcd_init(void)
{
    const spi_bus_config_t bus_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        FACULTY18_LCD_PIN_PCLK, FACULTY18_LCD_PIN_DATA0, FACULTY18_LCD_PIN_DATA1, FACULTY18_LCD_PIN_DATA2, FACULTY18_LCD_PIN_DATA3,
        FACULTY18_LCD_W * FACULTY18_LCD_H * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(FACULTY18_LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    if (s_flush_done == NULL) {
        s_flush_done = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_flush_done != NULL, ESP_ERR_NO_MEM, TAG, "flush sem");
    }
    const esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(FACULTY18_LCD_PIN_CS, lcd_flush_done_cb, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)FACULTY18_LCD_HOST, &io_cfg, &io), TAG, "lcd io");
    (void)s_panel_io;
    s_panel_io = io;

    sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = s_sh8601_init_cmds,
        .init_cmds_size = sizeof(s_sh8601_init_cmds) / sizeof(s_sh8601_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(io, &panel_cfg, &s_panel), TAG, "lcd panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "lcd reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "lcd init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "lcd on");

    s_fb = heap_caps_malloc(FACULTY18_LCD_W * FACULTY18_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(FACULTY18_LCD_W * FACULTY18_LCD_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(s_fb != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    faculty18_display_fill_rgb565(atom_ui_bg565());
    faculty18_display_flush_fb();
    return ESP_OK;
}

static esp_err_t faculty18_audio_init(void)
{
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << ATOM_PA_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "pa gpio");
    gpio_set_level(ATOM_PA_GPIO, 1);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx), TAG, "i2s chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(FACULTY18_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = ATOM_I2S_MCLK,
            .bclk = ATOM_I2S_BCK,
            .ws = ATOM_I2S_WS,
            .dout = ATOM_I2S_DOUT,
            .din = ATOM_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = ATOM_I2S_MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &std_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "i2s rx en");

    ESP_RETURN_ON_ERROR(faculty18_es8311_init(), TAG, "es8311");

    s_mic_probe_peak = faculty18_audio_probe_peak();
    if (s_mic_probe_peak < 32) {
        ESP_LOGE(TAG, "mic probe peak=%ld — ES8311 not capturing", (long)s_mic_probe_peak);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "mic probe peak=%ld", (long)s_mic_probe_peak);
    return ESP_OK;
}

bool faculty18_board_audio_ready(void)
{
    return s_audio_ready && s_es8311 != NULL;
}

bool faculty18_board_pi4ioe_ok(void)
{
    return s_tca9554_ok;
}

int32_t faculty18_board_mic_probe_peak(void)
{
    return s_mic_probe_peak;
}

esp_err_t faculty18_board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << ATOM_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&btn), TAG, "button gpio");

    ESP_RETURN_ON_ERROR(atom_i2c_init(), TAG, "i2c");
    if (faculty18_pmu_init() != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 PMU init failed — audio may be unavailable");
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    s_tca9554_ok = tca9554_power_on() == ESP_OK;
    if (!s_tca9554_ok) {
        ESP_LOGW(TAG, "TCA9554 power sequence failed — trying display init anyway");
    }

    s_audio_ready = faculty18_audio_init() == ESP_OK;
    ESP_RETURN_ON_ERROR(atom_lcd_init(), TAG, "lcd");
    if (!s_audio_ready) {
        ESP_LOGW(TAG, "ES8311 audio init failed — display-only mode");
    }
    ESP_LOGI(TAG, "Faculty18 ready (audio=%s)", s_audio_ready ? "ok" : "off");
    return ESP_OK;
}

void faculty18_board_set_backlight(uint8_t percent)
{
    (void)percent;
}

esp_err_t faculty18_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    if (!s_audio_ready || s_es8311 == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return atom_i2s_read_mono(samples, sample_count, out_read, timeout_ms);
}

esp_err_t faculty18_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    if (s_es8311 == NULL || s_i2s_tx == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes_written = 0;
    const size_t bytes = sample_count * sizeof(int16_t);
    const esp_err_t err =
        i2s_channel_write(s_i2s_tx, samples, bytes, &bytes_written, pdMS_TO_TICKS(timeout_ms));
    return (err == ESP_OK && bytes_written > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t faculty18_audio_set_sample_rate(uint32_t hz)
{
    if (s_es8311 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (hz == 0) {
        hz = FACULTY18_AUDIO_RATE;
    }
    ESP_RETURN_ON_ERROR(faculty18_i2s_set_rate(hz), TAG, "i2s rate");
    ESP_RETURN_ON_ERROR(
        es8311_sample_frequency_config(s_es8311, (int)(hz * ATOM_I2S_MCLK_MULTIPLE), (int)hz), TAG, "es8311 sf");
    return ESP_OK;
}

void faculty18_audio_set_speaker_mute(bool mute)
{
    gpio_set_level(ATOM_PA_GPIO, mute ? 0 : 1);
}

void faculty18_display_fill_rgb565(uint16_t color)
{
    if (s_fb == NULL) {
        return;
    }
    for (int i = 0; i < FACULTY18_LCD_W * FACULTY18_LCD_H; ++i) {
        s_fb[i] = color;
    }
}

void faculty18_display_fill_rect(int x, int y, int w, int h, uint16_t color)
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
    if (x + w > FACULTY18_LCD_W) {
        w = FACULTY18_LCD_W - x;
    }
    if (y + h > FACULTY18_LCD_H) {
        h = FACULTY18_LCD_H - y;
    }
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            s_fb[row * FACULTY18_LCD_W + col] = color;
        }
    }
}

void faculty18_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    faculty18_display_blit_rgb565_masked(pixels, NULL, x, y, w, h);
}

int faculty18_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL) {
        return 0;
    }
    int drawn = 0;
    for (int row = 0; row < h; ++row) {
        if (y + row < 0 || y + row >= FACULTY18_LCD_H) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            if (x + col < 0 || x + col >= FACULTY18_LCD_W) {
                continue;
            }
            const int idx = row * w + col;
            if (opaque != NULL && opaque[idx] == 0) {
                continue;
            }
            s_fb[(y + row) * FACULTY18_LCD_W + (x + col)] = pixels[idx];
            ++drawn;
        }
    }
    return drawn;
}

static void draw_char5x7(char c, int x, int y, uint16_t color)
{
    static const uint8_t font[95][5] = {
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
    if (c < 32 || c > 126) {
        c = '?';
    }
    const uint8_t *glyph = font[c - 32];
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if (bits & (1 << row)) {
                faculty18_display_fill_rect(x + col, y + row, 1, 1, color);
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
    int x = (FACULTY18_LCD_W - w) / 2;
    if (x < 0) {
        x = 0;
    }
    draw_text(text, x, y, color);
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

static void draw_faculty_portrait(const char *faculty_name, int x, int y, int w, int h, uint16_t accent)
{
    int bust_x = x;
    int bust_y = y;
    atom_faculty_bust_blit_origin(x, y, w, h, true, &bust_x, &bust_y);
    if (atom_faculty_draw_bust(bust_x, bust_y)) {
        return;
    }

    (void)accent;

    char initials[4];
    bust_initials(faculty_name, initials, sizeof(initials));
    const int text_w = (int)strlen(initials) * 6;
    const int text_x = x + (w - text_w) / 2;
    draw_text(initials, text_x, y + h / 2 - 3, rgb565(230, 235, 245));

    if (atom_faculty_bust_status() == ATOM_FACULTY_BUST_LOADING) {
        draw_text("load", x + w / 2 - 12, y + h - 16, rgb565(120, 130, 150));
    }
}

static void draw_setup_banner(faculty18_ui_state_t state, const char *detail, uint16_t ring)
{
    const char *banner = "FACULTY";

    switch (state) {
        case FACULTY18_UI_BOOT:
            banner = "BOOT";
            break;
        case FACULTY18_UI_WIFI:
            banner = "WIFI";
            break;
        case FACULTY18_UI_THINK:
            banner = "THINK";
            break;
        case FACULTY18_UI_SPEAK:
            banner = "SPEAK";
            break;
        case FACULTY18_UI_ERROR:
            banner = "ERROR";
            break;
        default:
            return;
    }

    faculty18_display_fill_rect(0, 0, FACULTY18_LCD_W, 16, rgb565(10, 12, 20));
    draw_centered_text(banner, 4, ring);
    if (detail != NULL && detail[0] != '\0') {
        draw_centered_text(detail, FACULTY18_LCD_H - 28, rgb565(120, 130, 150));
    }
}

void faculty18_display_draw_status(faculty18_ui_state_t state,
                            const char *faculty_name,
                            const char *detail)
{
    const uint16_t bg = atom_ui_bg565();
    faculty18_display_fill_rgb565(bg);

    uint16_t ring = rgb565(40, 245, 168);
    switch (state) {
        case FACULTY18_UI_BOOT:
            ring = rgb565(80, 120, 180);
            break;
        case FACULTY18_UI_WIFI:
            ring = rgb565(255, 184, 77);
            break;
        case FACULTY18_UI_LISTEN:
            ring = rgb565(40, 245, 168);
            break;
        case FACULTY18_UI_CAPTURE:
            ring = rgb565(255, 120, 80);
            break;
        case FACULTY18_UI_THINK:
            ring = rgb565(255, 184, 77);
            break;
        case FACULTY18_UI_SPEAK:
            ring = rgb565(120, 180, 255);
            break;
        case FACULTY18_UI_ERROR:
            ring = rgb565(255, 60, 60);
            break;
    }
    (void)ring;

    draw_faculty_portrait(faculty_name, 0, 0, FACULTY18_LCD_W, FACULTY18_LCD_H, ring);

    if (state != FACULTY18_UI_BOOT && state != FACULTY18_UI_LISTEN && state != FACULTY18_UI_CAPTURE) {
        draw_setup_banner(state, detail, ring);
    }

    if (s_panel != NULL && s_fb != NULL) {
        faculty18_display_flush_fb();
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

static uint16_t rgb565_panel_visible(uint16_t px)
{
    return rgb565_panel_wire(px);
}

size_t faculty18_display_bmp_size(void)
{
    const uint32_t row_stride = ((FACULTY18_LCD_W * 24u + 31u) / 32u) * 4u;
    return 54u + row_stride * (uint32_t)FACULTY18_LCD_H;
}

int faculty18_display_write_bmp(FILE *out)
{
    if (s_fb == NULL || out == NULL) {
        return -1;
    }

    const int w = FACULTY18_LCD_W;
    const int h = FACULTY18_LCD_H;
    const uint32_t row_stride = (((uint32_t)w * 24u + 31u) / 32u) * 4u;
    const uint32_t pixel_bytes = row_stride * (uint32_t)h;
    const uint32_t file_size = 54u + pixel_bytes;
    const size_t fb_bytes = (size_t)w * (size_t)h * sizeof(uint16_t);

    uint8_t *buf = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = (uint8_t *)malloc(file_size);
    }
    if (buf == NULL) {
        return -1;
    }

    uint16_t *snap = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (snap == NULL) {
        snap = (uint16_t *)malloc(fb_bytes);
    }
    if (snap != NULL) {
        memcpy(snap, s_fb, fb_bytes);
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
        const int py = h - 1 - yi;
        uint8_t *dst = pix + (uint32_t)yi * row_stride;
        for (int px = 0; px < w; ++px) {
            const uint16_t wired = fb_sample_rotated_ccw(px, py);
            const uint16_t c = rgb565_panel_wire(wired);
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
    }

    if (snap != NULL) {
        free(snap);
    }

    const size_t wrote = fwrite(buf, 1, file_size, out);
    free(buf);
    return wrote == file_size ? (int)file_size : -1;
}

bool faculty18_button_pressed(void)
{
    return gpio_get_level(ATOM_BUTTON_GPIO) == 0;
}

bool faculty18_button_just_pressed(void)
{
    const bool now = faculty18_button_pressed();
    const bool edge = now && !s_button_prev;
    s_button_prev = now;
    return edge;
}
