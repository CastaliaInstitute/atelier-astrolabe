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
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "atom_faculty.h"
#include "faculty175_board_id.h"
#include "faculty175_pmu.h"

static const char *TAG = "faculty_board";

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
#define FACULTY175_LCD_PIN_RST_175C GPIO_NUM_1  /* Waveshare ESP-IDF BSP_LCD_RST (1.75C) */
#define FACULTY175_LCD_PIN_RST_175 GPIO_NUM_2   /* Arduino pin_config LCD_RESET (1.75) */

#define FACULTY175_LCD_PANEL_GAP_X 0x06
#define FACULTY175_LCD_PANEL_GAP_Y 0

#define FACULTY175_I2C_PORT I2C_NUM_0
#define FACULTY175_I2S_PORT I2S_NUM_0

#define ATOM_AUDIO_I2C_SDA GPIO_NUM_15
#define ATOM_AUDIO_I2C_SCL GPIO_NUM_14
#define ATOM_I2S_MCLK GPIO_NUM_16
#define ATOM_I2S_BCK GPIO_NUM_9
#define ATOM_I2S_WS GPIO_NUM_45
#define ATOM_I2S_DOUT GPIO_NUM_8
#define ATOM_I2S_DIN GPIO_NUM_10
#define ATOM_PA_GPIO GPIO_NUM_46
#define ATOM_ES7210_ADDR ES7210_CODEC_DEFAULT_ADDR
#define ATOM_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR
#define FACULTY175_AUDIO_MIN_PROBE_PEAK 1
#define FACULTY175_AUDIO_WARN_PROBE_PEAK 32

#define ATOM_BUTTON_GPIO GPIO_NUM_0

#define FACULTY175_UI_BG_R 0
#define FACULTY175_UI_BG_G 0
#define FACULTY175_UI_BG_B 0

#define FACULTY175_UI_WAVE_H 48

/** Round panel geometry (466×466 visible circle). */
#define FACULTY175_PANEL_CX (FACULTY175_LCD_W / 2)
#define FACULTY175_PANEL_CY (FACULTY175_LCD_H / 2)
#define FACULTY175_BEZEL_OUTER_R 232
#define FACULTY175_BEZEL_INNER_R 214
#define FACULTY175_NAME_ARC_R 222

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
static uint16_t *s_fb;
static bool s_button_prev;
static uint16_t *s_flush_strip;

static const co5300_lcd_init_cmd_t s_co5300_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

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

#define FACULTY175_LCD_FLUSH_STRIP_H 16

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
    const size_t strip_bytes =
        (size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_FLUSH_STRIP_H * sizeof(uint16_t);
    /* SPI DMA on ESP32-S3 requires DMA-capable memory — non-DMA buffers never reach the panel. */
    s_flush_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    if (s_flush_strip == NULL) {
        s_flush_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (s_flush_strip == NULL) {
        ESP_LOGE(TAG, "lcd flush strip DMA alloc failed (%u bytes)", (unsigned)strip_bytes);
        return false;
    }
    return true;
}

static void faculty175_display_flush_fb(void)
{
    if (s_panel == NULL || s_fb == NULL || s_flush_done == NULL) {
        return;
    }
    if (!faculty175_flush_strip_alloc()) {
        return;
    }

    const int strip_h = FACULTY175_LCD_FLUSH_STRIP_H;
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
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(500)) != pdTRUE) {
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

static uint16_t atom_ui_bg565(void)
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

static esp_err_t faculty175_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = FACULTY175_I2C_PORT,
        .sda_io_num = ATOM_AUDIO_I2C_SDA,
        .scl_io_num = ATOM_AUDIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c_bus), TAG, "i2c bus");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static int32_t sample_abs(int16_t s)
{
    return s < 0 ? -(int32_t)s : (int32_t)s;
}

static esp_err_t faculty175_i2s_set_rate(uint32_t hz)
{
    if (s_i2s_tx == NULL || s_i2s_rx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_tx), TAG, "i2s tx dis");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_rx), TAG, "i2s rx dis");

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg), TAG, "i2s tx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_rx, &clk_cfg), TAG, "i2s rx clk");
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
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx), TAG, "i2s chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
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
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &std_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "i2s rx en");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = FACULTY175_I2S_PORT,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(s_i2s_data_if != NULL, ESP_ERR_NO_MEM, TAG, "i2s data if");
    return ESP_OK;
}

static esp_codec_dev_handle_t faculty175_spk_codec_init(void)
{
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = FACULTY175_I2C_PORT,
        .addr = ATOM_ES8311_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
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
        .pa_pin = ATOM_PA_GPIO,
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
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = FACULTY175_I2C_PORT,
        .addr = ATOM_ES7210_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (i2c_ctrl_if == NULL) {
        return NULL;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = i2c_ctrl_if,
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

static esp_err_t faculty175_codec_open(bool out, uint32_t hz)
{
    esp_codec_dev_handle_t dev = out ? s_spk_codec : s_mic_codec;
    if (dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = hz == 0 ? FACULTY175_AUDIO_RATE : hz,
    };
    return esp_codec_dev_open(dev, &fs);
}

static int32_t faculty175_audio_probe_peak(void)
{
    if (s_mic_codec == NULL) {
        return 0;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = FACULTY175_AUDIO_RATE,
    };
    if (esp_codec_dev_open(s_mic_codec, &fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_gain(s_mic_codec, 30.0f);

    int16_t frame[320];
    int32_t peak = 0;
    bool read_ok = false;
    vTaskDelay(pdMS_TO_TICKS(80));
    for (int i = 0; i < 8; ++i) {
        if (esp_codec_dev_read(s_mic_codec, frame, sizeof(frame)) != ESP_OK) {
            continue;
        }
        read_ok = true;
        for (size_t j = 0; j < 320; ++j) {
            const int32_t abs = sample_abs(frame[j]);
            if (abs > peak) {
                peak = abs;
            }
        }
    }
    esp_codec_dev_close(s_mic_codec);
    return read_ok ? peak : 0;
}

static esp_err_t faculty175_audio_init(void)
{
    ESP_RETURN_ON_ERROR(faculty175_i2c_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(faculty175_i2s_init(FACULTY175_AUDIO_RATE), TAG, "i2s");

    s_spk_codec = faculty175_spk_codec_init();
    s_mic_codec = faculty175_mic_codec_init();
    ESP_RETURN_ON_FALSE(s_spk_codec != NULL && s_mic_codec != NULL, ESP_FAIL, TAG, "codec init");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = FACULTY175_AUDIO_RATE,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_spk_codec, &fs), TAG, "spk open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_spk_codec, 70), TAG, "spk vol");
    ESP_RETURN_ON_ERROR(esp_codec_dev_close(s_spk_codec), TAG, "spk close");

    s_mic_probe_peak = faculty175_audio_probe_peak();
    if (s_mic_probe_peak < FACULTY175_AUDIO_MIN_PROBE_PEAK) {
        ESP_LOGE(TAG, "mic probe peak=%ld - ES7210 not capturing", (long)s_mic_probe_peak);
        return ESP_FAIL;
    }
    if (s_mic_probe_peak < FACULTY175_AUDIO_WARN_PROBE_PEAK) {
        ESP_LOGW(TAG, "mic probe peak=%ld below voice-quality threshold; continuing", (long)s_mic_probe_peak);
    }
    ESP_LOGI(TAG, "mic probe peak=%ld", (long)s_mic_probe_peak);
    ESP_RETURN_ON_ERROR(faculty175_codec_open(false, FACULTY175_AUDIO_RATE), TAG, "mic listen open");
    (void)esp_codec_dev_set_in_gain(s_mic_codec, 30.0f);
    return ESP_OK;
}

static void faculty175_lcd_hardware_reset(void)
{
    /* 1.75C BSP uses GPIO1; 1.75 Arduino examples use GPIO2 — pulse both. */
    const uint64_t mask = (1ULL << FACULTY175_LCD_PIN_RST_175C) | (1ULL << FACULTY175_LCD_PIN_RST_175);
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        return;
    }
    gpio_set_level(FACULTY175_LCD_PIN_RST_175C, 0);
    gpio_set_level(FACULTY175_LCD_PIN_RST_175, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(FACULTY175_LCD_PIN_RST_175C, 1);
    gpio_set_level(FACULTY175_LCD_PIN_RST_175, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

static esp_err_t faculty175_lcd_init(void)
{
    const spi_bus_config_t bus_cfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        FACULTY175_LCD_PIN_PCLK,
        FACULTY175_LCD_PIN_DATA0,
        FACULTY175_LCD_PIN_DATA1,
        FACULTY175_LCD_PIN_DATA2,
        FACULTY175_LCD_PIN_DATA3,
        FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(FACULTY175_LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    if (s_flush_done == NULL) {
        s_flush_done = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_flush_done != NULL, ESP_ERR_NO_MEM, TAG, "flush sem");
    }

    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(FACULTY175_LCD_PIN_CS, lcd_flush_done_cb, NULL);
    io_cfg.trans_queue_depth = 10;
    io_cfg.pclk_hz = 40 * 1000 * 1000;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)FACULTY175_LCD_HOST, &io_cfg, &s_panel_io), TAG, "lcd io");

    faculty175_lcd_hardware_reset();

    co5300_vendor_config_t vendor_cfg = {
        .init_cmds = s_co5300_init_cmds,
        .init_cmds_size = sizeof(s_co5300_init_cmds) / sizeof(s_co5300_init_cmds[0]),
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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_panel_io, &panel_cfg, &s_panel), TAG, "lcd panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, FACULTY175_LCD_PANEL_GAP_X, FACULTY175_LCD_PANEL_GAP_Y), TAG, "lcd gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "lcd init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "lcd on");

    s_fb = heap_caps_malloc(FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(s_fb != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    faculty175_display_fill_rgb565(atom_ui_bg565());
    faculty175_display_flush_fb();
    faculty175_board_set_backlight(100);
    ESP_LOGI(TAG, "CO5300 466×466 init ok (1.75C — not SH8601/1.8″)");
    return ESP_OK;
}

bool faculty175_board_audio_ready(void)
{
    return s_audio_ready && s_spk_codec != NULL && s_mic_codec != NULL;
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
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << ATOM_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&btn), TAG, "button gpio");

    ESP_RETURN_ON_ERROR(faculty175_i2c_init(), TAG, "i2c");
    if (faculty175_pmu_init() != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 PMU init failed — audio/display may be unavailable");
    }
    faculty175_board_log_identity();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(faculty175_lcd_init(), TAG, "lcd");

    s_audio_ready = faculty175_audio_init() == ESP_OK;
    if (!s_audio_ready) {
        ESP_LOGW(TAG, "ES7210/ES8311 audio init failed — display-only mode");
    }
    ESP_LOGI(TAG, "Faculty175 ready (audio=%s)", s_audio_ready ? "ok" : "off");
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

static esp_codec_dev_handle_t faculty175_active_codec(bool out)
{
    return out ? s_spk_codec : s_mic_codec;
}

esp_err_t faculty175_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_audio_ready || s_mic_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t bytes = sample_count * sizeof(int16_t);
    if (esp_codec_dev_read(s_mic_codec, samples, bytes) != ESP_OK) {
        return ESP_FAIL;
    }
    if (out_read != NULL) {
        *out_read = sample_count;
    }
    return ESP_OK;
}

esp_err_t faculty175_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_spk_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    static bool s_spk_open;
    if (!s_spk_open) {
        ESP_RETURN_ON_ERROR(faculty175_codec_open(true, FACULTY175_AUDIO_RATE), TAG, "spk open");
        ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_spk_codec, 70), TAG, "spk vol");
        s_spk_open = true;
    }

    const size_t bytes = sample_count * sizeof(int16_t);
    return esp_codec_dev_write(s_spk_codec, (void *)samples, (int)bytes) == ESP_OK ? ESP_OK : ESP_FAIL;
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
    (void)esp_codec_dev_close(s_mic_codec);
    ESP_RETURN_ON_ERROR(faculty175_i2s_set_rate(hz), TAG, "i2s rate");
    ESP_RETURN_ON_ERROR(faculty175_codec_open(true, hz), TAG, "spk open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_spk_codec, 70), TAG, "spk vol");
    ESP_RETURN_ON_ERROR(faculty175_codec_open(false, hz), TAG, "mic open");
    return ESP_OK;
}

void faculty175_audio_set_speaker_mute(bool mute)
{
    gpio_set_level(ATOM_PA_GPIO, mute ? 0 : 1);
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
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            s_fb[row * FACULTY175_LCD_W + col] = color;
        }
    }
}

void faculty175_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    faculty175_display_blit_rgb565_masked(pixels, NULL, x, y, w, h);
}

void faculty175_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL) {
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
            if (opaque != NULL && opaque[idx] < 128) {
                continue;
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

static void draw_bezel_ring(int cx, int cy, int outer_r, int inner_r, uint16_t color)
{
    if (s_fb == NULL || outer_r <= 0 || inner_r >= outer_r) {
        return;
    }
    const int outer_r2 = outer_r * outer_r;
    const int inner_r2 = inner_r * inner_r;
    const int y0 = cy - outer_r;
    const int y1 = cy + outer_r;
    for (int y = y0; y <= y1; ++y) {
        if (y < 0 || y >= FACULTY175_LCD_H) {
            continue;
        }
        const int dy = y - cy;
        const int dy2 = dy * dy;
        for (int x = cx - outer_r; x <= cx + outer_r; ++x) {
            if (x < 0 || x >= FACULTY175_LCD_W) {
                continue;
            }
            const int dx = x - cx;
            const int d2 = dx * dx + dy2;
            if (d2 <= outer_r2 && d2 >= inner_r2) {
                s_fb[y * FACULTY175_LCD_W + x] = color;
            }
        }
    }
}

static void draw_curved_text_top(const char *text, int cx, int cy, int radius, uint16_t color)
{
    if (text == NULL || text[0] == '\0' || radius <= 0) {
        return;
    }
    const int len = (int)strlen(text);
    if (len <= 0) {
        return;
    }
    const float char_w = 6.f;
    const float arc_len = char_w * (float)len;
    const float span = arc_len / (float)radius;
    const float start = -span * 0.5f;
    for (int i = 0; i < len; ++i) {
        const float t = start + ((float)i + 0.5f) * char_w / (float)radius;
        const int px = cx + (int)(radius * sinf(t)) - 2;
        const int py = cy - (int)(radius * cosf(t)) - 3;
        draw_char5x7(text[i], px, py, color);
    }
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

static void draw_input_waveform_overlay(int x,
                                        int y,
                                        int w,
                                        int h,
                                        const uint8_t *samples,
                                        const uint8_t *stream_mask,
                                        size_t count,
                                        uint16_t color_idle,
                                        uint16_t color_stream)
{
    if (samples == NULL || count == 0 || w <= 0 || h <= 2) {
        return;
    }

    const int mid = y + h / 2;

    for (int col = 0; col < w; ++col) {
        const size_t si = (count >= (size_t)w) ? (size_t)col : (size_t)col * count / (size_t)w;
        const uint8_t v = samples[si];
        int amp = ((int)v * (h - 2)) / 255;
        if (amp < 1 && v > 1) {
            amp = 1;
        }
        if (amp <= 0) {
            continue;
        }
        const uint16_t color =
            (stream_mask != NULL && stream_mask[si] > 0) ? color_stream : color_idle;
        int top = mid - amp;
        int bar_h = amp * 2 + 1;
        if (top < y) {
            bar_h -= y - top;
            top = y;
        }
        if (top + bar_h > y + h) {
            bar_h = y + h - top;
        }
        if (bar_h > 0) {
            faculty175_display_fill_rect(x + col, top, 1, bar_h, color);
        }
    }
}

static void draw_faculty_portrait(const char *faculty_name, int x, int y, int w, int h, uint16_t accent)
{
    const int bust_side = ATOM_FACULTY_BUST_W;
    const int bx = x + (w - bust_side) / 2;
    const int by = y + (h - bust_side) / 2 + ATOM_FACULTY_BUST_DISPLAY_Y_NUDGE;

    int bust_x = bx;
    int bust_y = by;
    atom_faculty_bust_blit_origin(bx, by, bust_side, bust_side, false, &bust_x, &bust_y);
    if (!atom_faculty_draw_bust(bust_x, bust_y)) {
        char initials[4];
        bust_initials(faculty_name, initials, sizeof(initials));
        const int text_w = (int)strlen(initials) * 6;
        const int text_x = bx + (bust_side - text_w) / 2;
        draw_text(initials, text_x, by + bust_side / 2 - 3, rgb565(230, 235, 245));

        if (atom_faculty_bust_status() == ATOM_FACULTY_BUST_LOADING) {
            draw_text("load", bx + bust_side / 2 - 12, by + bust_side - 16, rgb565(120, 130, 150));
        }
    }

    draw_bezel_ring(FACULTY175_PANEL_CX, FACULTY175_PANEL_CY, FACULTY175_BEZEL_OUTER_R, FACULTY175_BEZEL_INNER_R,
                    accent);

    char label[32];
    faculty_display_name(faculty_name, label, sizeof(label));
    draw_curved_text_top(label, FACULTY175_PANEL_CX, FACULTY175_PANEL_CY, FACULTY175_NAME_ARC_R, accent);
}

static void draw_setup_banner(faculty175_ui_state_t state, const char *detail, uint16_t ring)
{
    const char *banner = "FACULTY";

    switch (state) {
        case FACULTY175_UI_BOOT:
            banner = "BOOT";
            break;
        case FACULTY175_UI_WIFI:
            banner = "WIFI";
            break;
        case FACULTY175_UI_THINK:
            banner = "THINK";
            break;
        case FACULTY175_UI_SPEAK:
            banner = "SPEAK";
            break;
        case FACULTY175_UI_ERROR:
            banner = "ERROR";
            break;
        default:
            return;
    }

    faculty175_display_fill_rect(0, 0, FACULTY175_LCD_W, 16, rgb565(10, 12, 20));
    draw_centered_text(banner, 4, ring);
    if (detail != NULL && detail[0] != '\0') {
        draw_centered_text(detail, FACULTY175_LCD_H - 28, rgb565(120, 130, 150));
    }
}

void faculty175_display_draw_status(faculty175_ui_state_t state,
                                    const char *faculty_name,
                                    const char *detail,
                                    uint32_t anim_ms,
                                    const uint8_t *waveform,
                                    const uint8_t *waveform_stream,
                                    size_t waveform_len)
{
    const uint16_t bg = atom_ui_bg565();
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

    draw_faculty_portrait(faculty_name, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H, ring);

    if ((state == FACULTY175_UI_LISTEN || state == FACULTY175_UI_CAPTURE) && waveform != NULL && waveform_len > 0) {
        const int wave_h = FACULTY175_UI_WAVE_H;
        const int wave_y = FACULTY175_LCD_H - wave_h;
        const uint16_t wave_idle = rgb565(48, 210, 140);
        const uint16_t wave_stream = rgb565(255, 170, 90);
        draw_input_waveform_overlay(0,
                                    wave_y,
                                    FACULTY175_LCD_W,
                                    wave_h,
                                    waveform,
                                    waveform_stream,
                                    waveform_len,
                                    wave_idle,
                                    wave_stream);
    } else if (state != FACULTY175_UI_BOOT && state != FACULTY175_UI_LISTEN && state != FACULTY175_UI_CAPTURE) {
        draw_setup_banner(state, detail, ring);
    }

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
    }

    const size_t wrote = fwrite(buf, 1, file_size, out);
    free(buf);
    return wrote == file_size ? (int)file_size : -1;
}

bool faculty175_button_pressed(void)
{
    return gpio_get_level(ATOM_BUTTON_GPIO) == 0;
}

bool faculty175_button_just_pressed(void)
{
    const bool now = faculty175_button_pressed();
    const bool edge = now && !s_button_prev;
    s_button_prev = now;
    return edge;
}
