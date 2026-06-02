#include "atom_board.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "atom_faculty.h"

static const char *TAG = "atom_board";

/* AtomS3R 0.85" GC9107 SPI TFT — see M5GFX board_M5AtomS3R factory config. */
#define ATOM_LCD_HOST SPI3_HOST
#define ATOM_LCD_PIN_CS 14
#define ATOM_LCD_PIN_DC 42
#define ATOM_LCD_PIN_RST 48
#define ATOM_LCD_PIN_MOSI 21
#define ATOM_LCD_PIN_SCK 15
#define ATOM_SYS_I2C_SDA 45
#define ATOM_SYS_I2C_SCL 0
#define ATOM_LP5562_ADDR 0x30

/* Atomic Voice Base / Echo Base (ES8311) — M5EchoBase defaults for AtomS3R. */
#define ATOM_AUDIO_I2C_SDA 38
#define ATOM_AUDIO_I2C_SCL 39
#define ATOM_I2S_BCK 8
#define ATOM_I2S_WS 6
#define ATOM_I2S_DOUT 5
#define ATOM_I2S_DIN 7
#define ATOM_PI4IOE_ADDR 0x43
/* esp_codec_dev's ES8311 I2C config expects the 8-bit address; it shifts for IDF5. */
#define ATOM_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR

#define ATOM_BUTTON_GPIO 41

/* ESP-IDF's ST7789 wrapper plus GC9107 init needs less row offset than M5GFX's native panel. */
#define ATOM_PANEL_GAP_X 0
#define ATOM_PANEL_GAP_Y 0

#define ATOM_UI_BG_R 0
#define ATOM_UI_BG_G 0
#define ATOM_UI_BG_B 0

static i2c_master_bus_handle_t s_audio_i2c;
static i2c_master_bus_handle_t s_sys_i2c;
static esp_codec_dev_handle_t s_codec;
static bool s_audio_ready;
static bool s_pi4ioe_ok;
static int32_t s_mic_probe_peak;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;
static bool s_button_prev;

static uint16_t lcd_pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/** GC9107 uses INVON in hardware — framebuffer holds logical RGB565; panel inverts on output. */
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

static uint16_t atom_ui_bg565(void)
{
    return rgb565(ATOM_UI_BG_R, ATOM_UI_BG_G, ATOM_UI_BG_B);
}

uint16_t atom_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint16_t atom_display_fb_from_logical565(uint16_t logical565)
{
    return logical565;
}

uint16_t atom_display_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return lcd_pack565(r, g, b);
}

uint32_t atom_display_bkgd_u32(void)
{
    return ((uint32_t)ATOM_UI_BG_B << 16) | ((uint32_t)ATOM_UI_BG_G << 8) | ATOM_UI_BG_R;
}

static esp_err_t pi4ioe_transmit(uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ATOM_PI4IOE_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(s_audio_i2c, &cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t buf[2] = {reg, val};
    err = i2c_master_transmit(dev, buf, 2, 1000);
    i2c_master_bus_rm_device(dev);
    return err;
}

/** PI4IOE on Atomic Echo / Voice Base — matches M5Atomic-EchoBase pi4ioe_init(). */
static esp_err_t pi4ioe_enable_atomic_echo(void)
{
    esp_err_t err = pi4ioe_transmit(0x07, 0x00);
    if (err == ESP_OK) {
        err = pi4ioe_transmit(0x0D, 0xFF);
    }
    if (err == ESP_OK) {
        err = pi4ioe_transmit(0x03, 0x6F);
    }
    if (err == ESP_OK) {
        err = pi4ioe_transmit(0x05, 0xFF);
    }
    return err;
}

/** M5EchoBase::setMute(false) before record/play — PI4IOE OUT high enables analog path. */
static esp_err_t pi4ioe_set_mute(bool mute)
{
    return pi4ioe_transmit(0x05, mute ? 0x00 : 0xFF);
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
    (void)timeout_ms;
    const int ret = esp_codec_dev_read(s_codec, stereo, (int)bytes_want);
    if (ret != ESP_CODEC_DEV_OK) {
        free(stereo);
        return ESP_FAIL;
    }

    const size_t stereo_samples = bytes_want / sizeof(int16_t);
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

static int32_t atom_audio_probe_peak(void)
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

/**
 * ES8311 mic + playback routing for Atomic Voice Base.
 * Matches M5Unified _microphone_enabled_cb_atomic_echo + M5EchoBase es8311_microphone_config().
 */
static void es8311_tune_atomic_echo(esp_codec_dev_handle_t codec)
{
    if (codec == NULL) {
        return;
    }
    static const struct {
        uint8_t reg;
        uint8_t val;
    } seq[] = {
        {0x00, 0x80}, /* CSM power on */
        {0x01, 0xBA}, /* MCLK = BCLK (mic path) */
        {0x02, 0x18}, /* MULT_PRE = 3 */
        {0x0D, 0x01}, /* analog circuitry up */
        {0x0E, 0x02}, /* analog PGA + ADC modulator */
        {0x14, 0x1A}, /* analog mic, max PGA (EchoBase es8311_microphone_config) */
        {0x17, 0xFF}, /* ADC digital volume max */
        {0x1C, 0x6A}, /* EQ bypass, DC offset cancel */
        {0x12, 0x00}, /* DAC power-up (playback) */
        {0x13, 0x10}, /* HP driver enable */
        {0x32, 0xFF}, /* DAC volume */
        {0x37, 0x08}, /* DAC EQ bypass */
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
        if (esp_codec_dev_write_reg(codec, seq[i].reg, seq[i].val) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "ES8311 reg 0x%02x write failed", seq[i].reg);
        }
    }
}

static esp_err_t atom_lcd_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = ATOM_LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = ATOM_LCD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ATOM_LCD_W * ATOM_LCD_H * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ATOM_LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = ATOM_LCD_PIN_CS,
        .dc_gpio_num = ATOM_LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ATOM_LCD_HOST, &io_cfg, &io), TAG, "lcd io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = ATOM_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        /* ESP32 framebuffer is LE; driver defaults to BE RAMWR without this. */
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel), TAG, "lcd panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "lcd reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "lcd init");
    static const struct {
        uint8_t cmd;
        uint8_t data[14];
        uint8_t len;
        uint16_t delay_ms;
    } gc9107_init[] = {
        {0xFE, {0}, 0, 5},
        {0xEF, {0}, 0, 5},
        {0xB0, {0xC0}, 1, 0},
        {0xB2, {0x2F}, 1, 0},
        {0xB3, {0x03}, 1, 0},
        {0xB6, {0x19}, 1, 0},
        {0xB7, {0x01}, 1, 0},
        {0xAC, {0xCB}, 1, 0},
        {0xAB, {0x0E}, 1, 0},
        {0xB4, {0x04}, 1, 0},
        {0xA8, {0x19}, 1, 0},
        {0xB8, {0x08}, 1, 0},
        {0xE8, {0x24}, 1, 0},
        {0xE9, {0x48}, 1, 0},
        {0xEA, {0x22}, 1, 0},
        {0xC6, {0x30}, 1, 0},
        {0xC7, {0x18}, 1, 0},
        {0xF0, {0x01, 0x2b, 0x23, 0x3c, 0xb7, 0x12, 0x17, 0x60, 0x00, 0x06, 0x0c, 0x17, 0x12, 0x1f}, 14, 0},
        {0xF1, {0x05, 0x2e, 0x2d, 0x44, 0xd6, 0x15, 0x17, 0xa0, 0x02, 0x0d, 0x0d, 0x1a, 0x18, 0x1f}, 14, 0},
        {0x11, {0}, 0, 120},
        {0x29, {0}, 0, 0},
    };
    for (size_t i = 0; i < sizeof(gc9107_init) / sizeof(gc9107_init[0]); ++i) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, gc9107_init[i].cmd,
                                                       gc9107_init[i].len ? gc9107_init[i].data : NULL,
                                                       gc9107_init[i].len),
                            TAG, "gc9107 init");
        if (gc9107_init[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(gc9107_init[i].delay_ms));
        }
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "lcd invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, true), TAG, "lcd mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, ATOM_PANEL_GAP_X, ATOM_PANEL_GAP_Y), TAG, "lcd gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "lcd on");

    s_fb = heap_caps_malloc(ATOM_LCD_W * ATOM_LCD_H * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(ATOM_LCD_W * ATOM_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(s_fb != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    atom_display_fill_rgb565(atom_ui_bg565());
    return ESP_OK;
}

static esp_err_t atom_sys_i2c_write(uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ATOM_LP5562_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_sys_i2c, &cfg, &dev), TAG, "lp5562 add");
    const uint8_t buf[2] = {reg, val};
    const esp_err_t err = i2c_master_transmit(dev, buf, 2, 1000);
    i2c_master_bus_rm_device(dev);
    return err;
}

static esp_err_t atom_backlight_init(uint8_t brightness)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = ATOM_SYS_I2C_SDA,
        .scl_io_num = ATOM_SYS_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_sys_i2c), TAG, "sys i2c bus");
    ESP_RETURN_ON_ERROR(atom_sys_i2c_write(0x00, 0x40), TAG, "lp5562 enable");
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_RETURN_ON_ERROR(atom_sys_i2c_write(0x08, 0x01), TAG, "lp5562 led map");
    ESP_RETURN_ON_ERROR(atom_sys_i2c_write(0x70, 0x00), TAG, "lp5562 charge pump");
    ESP_RETURN_ON_ERROR(atom_sys_i2c_write(0x0E, brightness), TAG, "lp5562 backlight");
    return ESP_OK;
}

static esp_err_t atom_audio_init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = ATOM_AUDIO_I2C_SDA,
        .scl_io_num = ATOM_AUDIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_audio_i2c), TAG, "i2c bus");

    /* M5EchoBase::init order: I2C → I2S → ES8311 → PI4IOE (RecordPlay.ino). */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx), TAG, "i2s chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ATOM_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
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

    audio_codec_i2s_cfg_t i2s_codec_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_codec_cfg);

    audio_codec_i2c_cfg_t i2c_codec_cfg = {
        .port = I2C_NUM_0,
        .addr = ATOM_ES8311_ADDR,
        .bus_handle = s_audio_i2c,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_codec_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = GPIO_NUM_NC,
        .use_mclk = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec != NULL, ESP_FAIL, TAG, "codec dev");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = ATOM_AUDIO_RATE,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_codec, &fs), TAG, "codec open");
    es8311_tune_atomic_echo(s_codec);
    if (esp_codec_dev_set_in_gain(s_codec, 42.0f) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "mic gain set failed — using register tune");
    }
    if (esp_codec_dev_set_out_vol(s_codec, 70) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "speaker volume set failed");
    }

    s_pi4ioe_ok = pi4ioe_enable_atomic_echo() == ESP_OK;
    if (!s_pi4ioe_ok) {
        ESP_LOGE(TAG, "PI4IOE init failed — stack Atomic Voice Base on AtomS3R");
        return ESP_FAIL;
    }
    if (pi4ioe_set_mute(false) != ESP_OK) {
        ESP_LOGW(TAG, "PI4IOE unmute failed");
    }

    s_mic_probe_peak = atom_audio_probe_peak();
    if (s_mic_probe_peak < 32) {
        ESP_LOGE(TAG, "mic probe peak=%ld — ES8311 not capturing (check Voice Base stack)",
                 (long)s_mic_probe_peak);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "mic probe peak=%ld", (long)s_mic_probe_peak);
    return ESP_OK;
}

bool atom_board_audio_ready(void)
{
    return s_audio_ready && s_codec != NULL;
}

bool atom_board_pi4ioe_ok(void)
{
    return s_pi4ioe_ok;
}

int32_t atom_board_mic_probe_peak(void)
{
    return s_mic_probe_peak;
}

esp_err_t atom_board_init(void)
{
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << ATOM_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&btn), TAG, "button gpio");

    ESP_RETURN_ON_ERROR(atom_backlight_init(180), TAG, "backlight");
    ESP_RETURN_ON_ERROR(atom_lcd_init(), TAG, "lcd");
    s_audio_ready = atom_audio_init() == ESP_OK;
    if (!s_audio_ready) {
        ESP_LOGW(TAG, "Voice Base audio init failed — display-only mode");
    }
    ESP_LOGI(TAG, "FacultyAtom ready (audio=%s)", s_audio_ready ? "ok" : "off");
    return ESP_OK;
}

void atom_board_set_backlight(uint8_t percent)
{
    if (s_sys_i2c == NULL) {
        return;
    }
    const uint8_t brightness = (uint8_t)((uint16_t)percent * 255 / 100);
    if (atom_sys_i2c_write(0x0E, brightness) != ESP_OK) {
        ESP_LOGW(TAG, "backlight set failed");
    }
}

esp_err_t atom_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    if (!s_audio_ready || s_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return atom_i2s_read_mono(samples, sample_count, out_read, timeout_ms);
}

esp_err_t atom_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    if (s_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_write(s_codec, (void *)samples, sample_count * sizeof(int16_t));
    (void)timeout_ms;
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t atom_audio_set_sample_rate(uint32_t hz)
{
    if (s_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = hz,
    };
    return esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

void atom_audio_set_speaker_mute(bool mute)
{
    if (s_pi4ioe_ok) {
        (void)pi4ioe_set_mute(mute);
    }
    if (s_codec != NULL) {
        (void)esp_codec_dev_set_out_mute(s_codec, mute);
    }
}

void atom_display_fill_rgb565(uint16_t color)
{
    if (s_fb == NULL) {
        return;
    }
    for (int i = 0; i < ATOM_LCD_W * ATOM_LCD_H; ++i) {
        s_fb[i] = color;
    }
    if (s_panel != NULL) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, ATOM_LCD_W, ATOM_LCD_H, s_fb);
    }
}

void atom_display_fill_rect(int x, int y, int w, int h, uint16_t color)
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
    if (x + w > ATOM_LCD_W) {
        w = ATOM_LCD_W - x;
    }
    if (y + h > ATOM_LCD_H) {
        h = ATOM_LCD_H - y;
    }
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            s_fb[row * ATOM_LCD_W + col] = color;
        }
    }
}

void atom_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    atom_display_blit_rgb565_masked(pixels, NULL, x, y, w, h);
}

void atom_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h)
{
    if (s_fb == NULL || pixels == NULL) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        if (y + row < 0 || y + row >= ATOM_LCD_H) {
            continue;
        }
        for (int col = 0; col < w; ++col) {
            if (x + col < 0 || x + col >= ATOM_LCD_W) {
                continue;
            }
            const int idx = row * w + col;
            if (opaque != NULL && opaque[idx] == 0) {
                continue;
            }
            s_fb[(y + row) * ATOM_LCD_W + (x + col)] = pixels[idx];
        }
    }
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
                atom_display_fill_rect(x + col, y + row, 1, 1, color);
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

static void draw_rect_outline(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 1 || h <= 1) {
        return;
    }
    atom_display_fill_rect(x, y, w, 1, color);
    atom_display_fill_rect(x, y + h - 1, w, 1, color);
    atom_display_fill_rect(x, y, 1, h, color);
    atom_display_fill_rect(x + w - 1, y, 1, h, color);
}

static void draw_faculty_face_marks(uint16_t color)
{
    (void)color;
}

static void draw_centered_text(const char *text, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    int x = (ATOM_LCD_W - w) / 2;
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
    if (atom_faculty_draw_bust(x, y)) {
        return;
    }

    (void)accent;

    char initials[4];
    bust_initials(faculty_name, initials, sizeof(initials));
    const int text_w = (int)strlen(initials) * 6;
    const int text_x = x + (w - text_w) / 2;
    draw_rect_outline(x + 28, y + 16, w - 56, h - 32, rgb565(40, 52, 68));
    atom_display_fill_rect(x + 49, y + 35, 30, 24, rgb565(40, 52, 68));
    atom_display_fill_rect(x + 38, y + 67, 52, 28, rgb565(40, 52, 68));
    draw_text(initials, text_x, y + h / 2 - 3, rgb565(230, 235, 245));

    if (atom_faculty_bust_status() == ATOM_FACULTY_BUST_LOADING) {
        draw_text("load", x + w / 2 - 12, y + h - 16, rgb565(120, 130, 150));
    }
}

static void draw_setup_banner(atom_ui_state_t state, const char *detail, uint16_t ring)
{
    const char *banner = "FACE";

    switch (state) {
        case ATOM_UI_BOOT:
            banner = "BOOT";
            break;
        case ATOM_UI_WIFI:
            banner = "WIFI";
            break;
        case ATOM_UI_THINK:
            banner = "THINK";
            break;
        case ATOM_UI_SPEAK:
            banner = "SPEAK";
            break;
        case ATOM_UI_ERROR:
            banner = "ERR";
            break;
        default:
            return;
    }

    (void)banner;
    if (detail != NULL && detail[0] != '\0') {
        draw_centered_text(detail, 116, rgb565(120, 130, 150));
    }
}

void atom_display_draw_status(atom_ui_state_t state,
                            const char *faculty_name,
                            const char *detail,
                            uint32_t anim_ms,
                            const uint8_t *waveform,
                            size_t waveform_len)
{
    const uint16_t bg = atom_ui_bg565();
    atom_display_fill_rgb565(bg);

    uint16_t ring = rgb565(40, 245, 168);
    switch (state) {
        case ATOM_UI_BOOT:
            ring = rgb565(80, 120, 180);
            break;
        case ATOM_UI_WIFI:
            ring = rgb565(255, 184, 77);
            break;
        case ATOM_UI_LISTEN:
            ring = rgb565(40, 245, 168);
            break;
        case ATOM_UI_CAPTURE:
            ring = rgb565(255, 120, 80);
            break;
        case ATOM_UI_THINK:
            ring = rgb565(255, 184, 77);
            break;
        case ATOM_UI_SPEAK:
            ring = rgb565(120, 180, 255);
            break;
        case ATOM_UI_ERROR:
            ring = rgb565(255, 60, 60);
            break;
    }
    (void)ring;
    (void)anim_ms;
    (void)waveform;
    (void)waveform_len;

    draw_faculty_portrait(faculty_name, 0, 0, ATOM_LCD_W, ATOM_LCD_H, ring);
    draw_faculty_face_marks(ring);

    if (state != ATOM_UI_BOOT && state != ATOM_UI_LISTEN && state != ATOM_UI_CAPTURE) {
        draw_setup_banner(state, detail, ring);
    }

    if (s_panel != NULL && s_fb != NULL) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, ATOM_LCD_W, ATOM_LCD_H, s_fb);
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
    const uint8_t r5 = (uint8_t)(31u - ((px >> 11) & 0x1Fu));
    const uint8_t g6 = (uint8_t)(63u - ((px >> 5) & 0x3Fu));
    const uint8_t b5 = (uint8_t)(31u - (px & 0x1Fu));
    return (uint16_t)(((uint16_t)r5 << 11) | ((uint16_t)g6 << 5) | b5);
}

size_t atom_display_bmp_size(void)
{
    const uint32_t row_stride = ((ATOM_LCD_W * 24u + 31u) / 32u) * 4u;
    return 54u + row_stride * (uint32_t)ATOM_LCD_H;
}

int atom_display_write_bmp(FILE *out)
{
    if (s_fb == NULL || out == NULL) {
        return -1;
    }

    const int w = ATOM_LCD_W;
    const int h = ATOM_LCD_H;
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
        const int y = h - 1 - yi;
        uint8_t *dst = pix + (uint32_t)yi * row_stride;
        for (int x = 0; x < w; ++x) {
            const uint16_t raw = snap != NULL ? snap[y * w + x] : s_fb[y * w + x];
            const uint16_t c = rgb565_panel_visible(raw);
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

bool atom_button_pressed(void)
{
    return gpio_get_level(ATOM_BUTTON_GPIO) == 0;
}

bool atom_button_just_pressed(void)
{
    const bool now = atom_button_pressed();
    const bool edge = now && !s_button_prev;
    s_button_prev = now;
    return edge;
}
