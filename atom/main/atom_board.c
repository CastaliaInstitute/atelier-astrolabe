#include "atom_board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
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

static const char *TAG = "atom_board";

/* AtomS3R 0.85" GC9107 (ST7789-class SPI) — see M5 AtomS3R pin map. */
#define ATOM_LCD_HOST SPI2_HOST
#define ATOM_LCD_PIN_CS 14
#define ATOM_LCD_PIN_DC 42
#define ATOM_LCD_PIN_RST 48
#define ATOM_LCD_PIN_MOSI 21
#define ATOM_LCD_PIN_SCK 15

/* Atomic Voice Base / Echo Base (ES8311) — M5EchoBase defaults for AtomS3R. */
#define ATOM_AUDIO_I2C_SDA 38
#define ATOM_AUDIO_I2C_SCL 39
#define ATOM_I2S_BCK 8
#define ATOM_I2S_WS 6
#define ATOM_I2S_DOUT 5
#define ATOM_I2S_DIN 7
#define ATOM_PI4IOE_ADDR 0x43
#define ATOM_ES8311_ADDR 0x18

#define ATOM_BUTTON_GPIO 41

static i2c_master_bus_handle_t s_audio_i2c;
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t s_i2s_tx;
static i2s_chan_handle_t s_i2s_rx;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;
static bool s_button_prev;

static esp_err_t pi4ioe_enable_speaker(void)
{
    uint8_t buf[2];
    i2c_device_handle_t dev;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ATOM_PI4IOE_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_audio_i2c, &cfg, &dev), TAG, "pi4ioe add");

    buf[0] = 0x07;
    buf[1] = 0x00;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, buf, 2, 1000), TAG, "pi4ioe pp");
    buf[0] = 0x0D;
    buf[1] = 0xFF;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, buf, 2, 1000), TAG, "pi4ioe pull");
    buf[0] = 0x03;
    buf[1] = 0x6F;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, buf, 2, 1000), TAG, "pi4ioe dir");
    buf[0] = 0x05;
    buf[1] = 0xFF;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, buf, 2, 1000), TAG, "pi4ioe out");

    i2c_master_bus_rm_device(dev);
    return ESP_OK;
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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel), TAG, "lcd panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "lcd reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "lcd init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "lcd invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, true), TAG, "lcd mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "lcd on");

    s_fb = heap_caps_malloc(ATOM_LCD_W * ATOM_LCD_H * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(ATOM_LCD_W * ATOM_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(s_fb != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer");
    atom_display_fill_rgb565(0x0000);
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
    ESP_RETURN_ON_ERROR(pi4ioe_enable_speaker(), TAG, "pi4ioe");

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
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = ATOM_AUDIO_RATE,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_codec, &fs), TAG, "codec open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_codec, 30.0f), TAG, "mic gain");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_codec, 70), TAG, "spk vol");
    return ESP_OK;
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

    ESP_RETURN_ON_ERROR(atom_lcd_init(), TAG, "lcd");
    ESP_RETURN_ON_ERROR(atom_audio_init(), TAG, "audio");
    ESP_LOGI(TAG, "AtomS3R + Voice Base ready");
    return ESP_OK;
}

void atom_board_set_backlight(uint8_t percent)
{
    (void)percent;
}

esp_err_t atom_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms)
{
    if (s_codec == NULL || samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_read(s_codec, samples, sample_count * sizeof(int16_t));
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    if (out_read != NULL) {
        *out_read = sample_count;
    }
    (void)timeout_ms;
    return ESP_OK;
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
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = hz,
    };
    return esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

void atom_audio_set_speaker_mute(bool mute)
{
    if (s_codec != NULL) {
        (void)esp_codec_dev_set_out_mute(s_codec, mute);
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
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
            s_fb[(y + row) * ATOM_LCD_W + (x + col)] = pixels[row * w + col];
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

void atom_display_draw_status(atom_ui_state_t state, const char *faculty_name, const char *detail)
{
    const uint16_t bg = rgb565(6, 8, 16);
    const uint16_t fg = rgb565(220, 230, 240);
    atom_display_fill_rgb565(bg);

    const char *label = "WAND";
    uint16_t ring = rgb565(40, 245, 168);
    switch (state) {
        case ATOM_UI_BOOT:
            label = "BOOT";
            ring = rgb565(80, 120, 180);
            break;
        case ATOM_UI_WIFI:
            label = "WIFI";
            ring = rgb565(255, 184, 77);
            break;
        case ATOM_UI_LISTEN:
            label = "LISTEN";
            break;
        case ATOM_UI_CAPTURE:
            label = "HEAR";
            ring = rgb565(255, 120, 80);
            break;
        case ATOM_UI_THINK:
            label = "THINK";
            ring = rgb565(255, 184, 77);
            break;
        case ATOM_UI_SPEAK:
            label = "SPEAK";
            ring = rgb565(120, 180, 255);
            break;
        case ATOM_UI_ERROR:
            label = "ERR";
            ring = rgb565(255, 60, 60);
            break;
    }

    atom_display_fill_rect(8, 8, ATOM_LCD_W - 16, ATOM_LCD_H - 16, rgb565(12, 16, 28));
    atom_display_fill_rect(12, 12, ATOM_LCD_W - 24, 3, ring);
    draw_text(label, 16, 24, ring);
    if (faculty_name != NULL && faculty_name[0] != '\0') {
        draw_text(faculty_name, 16, 40, fg);
    }
    if (detail != NULL && detail[0] != '\0') {
        draw_text(detail, 16, 56, rgb565(140, 150, 165));
    }

    if (s_panel != NULL && s_fb != NULL) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, ATOM_LCD_W, ATOM_LCD_H, s_fb);
    }
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
