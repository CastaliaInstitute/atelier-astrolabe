#include "paper_epd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "paper_epd";

#define PAPER_EPD_HOST SPI2_HOST
#define PAPER_EPD_PIN_SCLK GPIO_NUM_15
#define PAPER_EPD_PIN_MOSI GPIO_NUM_13
#define PAPER_EPD_PIN_MISO GPIO_NUM_14
#define PAPER_EPD_PIN_CS GPIO_NUM_44
#define PAPER_EPD_PIN_DC GPIO_NUM_43
#define PAPER_EPD_PIN_RST GPIO_NUM_12
#define PAPER_EPD_PIN_BUSY GPIO_NUM_11

#define PAPER_EPD_PANEL_W 400
#define PAPER_EPD_PANEL_H 600
#define PAPER_EPD_ROW_BYTES ((PAPER_EPD_PANEL_W + 1) / 2)

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t idx;
} epd_palette_t;

static const epd_palette_t s_epd_palette[] = {
    {0, 0, 0, 0x0},
    {255, 255, 255, 0x1},
    {255, 243, 56, 0x2},
    {191, 0, 0, 0x3},
    {100, 64, 255, 0x5},
    {67, 138, 28, 0x6},
};

static const uint8_t s_init_cmds[] = {
    0xAA, 6, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18,
    0x01, 1, 0x3F,
    0x00, 2, 0x5F, 0x69,
    0x05, 4, 0x40, 0x1F, 0x1F, 0x2C,
    0x08, 4, 0x6F, 0x1F, 0x1F, 0x22,
    0x06, 4, 0x6F, 0x1F, 0x17, 0x17,
    0x03, 4, 0x03, 0x54, 0x00, 0x44,
    0x60, 2, 0x02, 0x00,
    0x30, 1, 0x08,
    0x50, 1, 0x3F,
    0xE3, 1, 0x2F,
    0x84, 1, 0x01,
    0xFF, 0xFF,
};

static spi_device_handle_t s_epd_spi;
static bool s_epd_ready;
static paper_epd_mode_t s_epd_mode = PAPER_EPD_MODE_QUALITY;

static void epd_select(bool selected)
{
    gpio_set_level(PAPER_EPD_PIN_CS, selected ? 0 : 1);
}

static esp_err_t epd_tx(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    return spi_device_polling_transmit(s_epd_spi, &t);
}

static esp_err_t epd_cmd(uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(PAPER_EPD_PIN_DC, 0), TAG, "dc cmd");
    return epd_tx(&cmd, 1);
}

static esp_err_t epd_data(const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(PAPER_EPD_PIN_DC, 1), TAG, "dc data");
    return epd_tx(data, len);
}

static esp_err_t epd_data_u8(uint8_t data)
{
    return epd_data(&data, 1);
}

static esp_err_t epd_wait_busy(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    while (gpio_get_level(PAPER_EPD_PIN_BUSY) == 0) {
        if ((xTaskGetTickCount() - start) > timeout) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

static esp_err_t epd_send_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_ERROR(epd_wait_busy(20000), TAG, "busy before init cmd");
    ESP_RETURN_ON_ERROR(epd_cmd(cmd), TAG, "cmd");
    ESP_RETURN_ON_ERROR(epd_data(data, len), TAG, "data");
    return ESP_OK;
}

static esp_err_t epd_init_sequence(void)
{
    epd_select(true);
    const uint8_t *p = s_init_cmds;
    while (!(p[0] == 0xFF && p[1] == 0xFF)) {
        const uint8_t cmd = p[0];
        const uint8_t len = p[1];
        esp_err_t err = epd_send_cmd_data(cmd, &p[2], len);
        if (err != ESP_OK) {
            epd_select(false);
            ESP_RETURN_ON_ERROR(err, TAG, "init list");
        }
        p += 2 + len;
    }

    const uint8_t resolution[] = {
        (uint8_t)(PAPER_EPD_PANEL_W >> 8),
        (uint8_t)PAPER_EPD_PANEL_W,
        (uint8_t)(PAPER_EPD_PANEL_H >> 8),
        (uint8_t)PAPER_EPD_PANEL_H,
    };
    esp_err_t err = epd_send_cmd_data(0x61, resolution, sizeof(resolution));
    epd_select(false);
    ESP_RETURN_ON_ERROR(err, TAG, "resolution");
    return ESP_OK;
}

static esp_err_t epd_refresh(void)
{
    esp_err_t ret = ESP_OK;
    epd_select(true);
    ESP_GOTO_ON_ERROR(epd_cmd(0x04), out, TAG, "power on");
    ESP_GOTO_ON_ERROR(epd_wait_busy(20000), out, TAG, "busy power on");
    vTaskDelay(pdMS_TO_TICKS(200));

    const uint8_t booster[] = {0x6F, 0x1F, 0x17, 0x27};
    ESP_GOTO_ON_ERROR(epd_cmd(0x06), out, TAG, "booster");
    ESP_GOTO_ON_ERROR(epd_data(booster, sizeof(booster)), out, TAG, "booster data");
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_GOTO_ON_ERROR(epd_cmd(0x12), out, TAG, "refresh");
    ESP_GOTO_ON_ERROR(epd_data_u8(0x00), out, TAG, "refresh data");
    ESP_GOTO_ON_ERROR(epd_wait_busy(20000), out, TAG, "busy refresh");

    ESP_GOTO_ON_ERROR(epd_cmd(0x02), out, TAG, "power off");
    ESP_GOTO_ON_ERROR(epd_data_u8(0x00), out, TAG, "power off data");
    ESP_GOTO_ON_ERROR(epd_wait_busy(20000), out, TAG, "busy power off");
    vTaskDelay(pdMS_TO_TICKS(200));
out:
    epd_select(false);
    return ret;
}

static uint8_t rgb565_to_epd(uint16_t c)
{
    const int32_t r = (int32_t)(((c >> 11) & 0x1F) * 255 / 31);
    const int32_t g = (int32_t)(((c >> 5) & 0x3F) * 255 / 63);
    const int32_t b = (int32_t)((c & 0x1F) * 255 / 31);
    uint32_t best_dist = UINT32_MAX;
    uint8_t best = 0x1;
    for (size_t i = 0; i < sizeof(s_epd_palette) / sizeof(s_epd_palette[0]); ++i) {
        const int32_t dr = r - s_epd_palette[i].r;
        const int32_t dg = g - s_epd_palette[i].g;
        const int32_t db = b - s_epd_palette[i].b;
        const uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (dist < best_dist) {
            best_dist = dist;
            best = s_epd_palette[i].idx;
        }
    }
    return best;
}

static void rgb565_to_rgb888(uint16_t c, int32_t *r, int32_t *g, int32_t *b)
{
    *r = (int32_t)(((c >> 11) & 0x1F) * 255 / 31);
    *g = (int32_t)(((c >> 5) & 0x3F) * 255 / 63);
    *b = (int32_t)((c & 0x1F) * 255 / 31);
}

static uint8_t rgb_pair_to_epd(int32_t r0, int32_t g0, int32_t b0, int32_t r1, int32_t g1, int32_t b1)
{
    uint32_t best_dist = UINT32_MAX;
    uint8_t best = 0x11;
    int32_t r0diff[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t g0diff[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t b0diff[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t r1diff[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t g1diff[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t b1diff[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t indiv0[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];
    int32_t indiv1[sizeof(s_epd_palette) / sizeof(s_epd_palette[0])];

    for (size_t i = 0; i < sizeof(s_epd_palette) / sizeof(s_epd_palette[0]); ++i) {
        r0diff[i] = r0 - s_epd_palette[i].r;
        g0diff[i] = g0 - s_epd_palette[i].g;
        b0diff[i] = b0 - s_epd_palette[i].b;
        r1diff[i] = r1 - s_epd_palette[i].r;
        g1diff[i] = g1 - s_epd_palette[i].g;
        b1diff[i] = b1 - s_epd_palette[i].b;
        indiv0[i] = r0diff[i] * r0diff[i] + g0diff[i] * g0diff[i] + b0diff[i] * b0diff[i];
        indiv1[i] = r1diff[i] * r1diff[i] + g1diff[i] * g1diff[i] + b1diff[i] * b1diff[i];
    }

    for (size_t i = 0; i < sizeof(s_epd_palette) / sizeof(s_epd_palette[0]); ++i) {
        for (size_t j = 0; j < sizeof(s_epd_palette) / sizeof(s_epd_palette[0]); ++j) {
            const int32_t dr = r0diff[i] + r1diff[j];
            const int32_t dg = g0diff[i] + g1diff[j];
            const int32_t db = b0diff[i] + b1diff[j];
            const uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db) + (uint32_t)(indiv0[i] + indiv1[j]);
            if (dist < best_dist) {
                best_dist = dist;
                best = (uint8_t)((s_epd_palette[i].idx << 4) | s_epd_palette[j].idx);
            }
        }
    }
    return best;
}

static int32_t clamp8(int32_t v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return v;
}

static uint8_t rgb565_pair_to_epd_text(uint16_t c0, uint16_t c1, int x, int y, uint8_t dither)
{
    const int32_t x_step = 127 * 29;
    const int32_t y_step = 129 * 48;
    const int32_t step_value = 129 * 127;
    const int32_t step_diff = step_value / 3;
    int32_t bias_base = (int32_t)y * y_step % step_value;

    int32_t rgb[2][3] = {{128, 128, 128}, {128, 128, 128}};
    rgb565_to_rgb888(c0, &rgb[0][0], &rgb[0][1], &rgb[0][2]);
    rgb565_to_rgb888(c1, &rgb[1][0], &rgb[1][1], &rgb[1][2]);

    for (int i = 0; i < 2; ++i) {
        bias_base -= x_step * (i == 0 ? x + 1 : 1);
        while (bias_base < 0) {
            bias_base += step_value;
        }
        int32_t bias_r = bias_base;
        int32_t bias_g = bias_base - step_diff;
        if (bias_g < 0) {
            bias_g += step_value;
        }
        int32_t bias_b = bias_g - step_diff;
        if (bias_b < 0) {
            bias_b += step_value;
        }
        bias_b = bias_b * 2 - (step_value - 1);
        bias_g = bias_g * 2 - (step_value - 1);
        bias_r = bias_r * 2 - (step_value - 1);
        const int32_t bias = ((bias_r + bias_g + bias_b) * dither) >> 16;
        rgb[i][0] = clamp8(rgb[i][0] + bias + ((bias_r * dither) >> 16));
        rgb[i][1] = clamp8(rgb[i][1] + bias + ((bias_g * dither) >> 16));
        rgb[i][2] = clamp8(rgb[i][2] + bias + ((bias_b * dither) >> 16));
    }
    return rgb_pair_to_epd(rgb[0][0], rgb[0][1], rgb[0][2], rgb[1][0], rgb[1][1], rgb[1][2]);
}

static uint8_t rgb565_pair_to_epd_fast(uint16_t c0, uint16_t c1, int x, int y)
{
    static const uint8_t bayer4[4][4] = {
        {0, 128, 32, 160},
        {192, 64, 224, 96},
        {48, 176, 16, 144},
        {240, 112, 208, 80},
    };
    int32_t r0, g0, b0;
    int32_t r1, g1, b1;
    rgb565_to_rgb888(c0, &r0, &g0, &b0);
    rgb565_to_rgb888(c1, &r1, &g1, &b1);
    int32_t bias = ((int32_t)bayer4[y & 3][x & 3] * 2 - 255) * 70 >> 8;
    r0 = clamp8(r0 + bias);
    g0 = clamp8(g0 + bias);
    b0 = clamp8(b0 + bias);
    bias = ((int32_t)bayer4[y & 3][(x + 1) & 3] * 2 - 255) * 70 >> 8;
    r1 = clamp8(r1 + bias);
    g1 = clamp8(g1 + bias);
    b1 = clamp8(b1 + bias);
    return rgb_pair_to_epd(r0, g0, b0, r1, g1, b1);
}

static void epd_convert_row_rgb565(const uint16_t *src, uint8_t *dst, int y, int w)
{
    for (int x = 0; x < w; x += 2) {
        const uint16_t c0 = src[x];
        const uint16_t c1 = (x + 1 < w) ? src[x + 1] : 0xFFFF;
        switch (s_epd_mode) {
            case PAPER_EPD_MODE_FASTEST:
                dst[x >> 1] = (uint8_t)((rgb565_to_epd(c0) << 4) | rgb565_to_epd(c1));
                break;
            case PAPER_EPD_MODE_FAST:
                dst[x >> 1] = rgb565_pair_to_epd_fast(c0, c1, x, y);
                break;
            case PAPER_EPD_MODE_TEXT:
                dst[x >> 1] = rgb565_pair_to_epd_text(c0, c1, x, y, 70);
                break;
            case PAPER_EPD_MODE_QUALITY:
            default:
                dst[x >> 1] = rgb565_pair_to_epd_text(c0, c1, x, y, 140);
                break;
        }
    }
}

void paper_epd_set_mode(paper_epd_mode_t mode)
{
    if (mode >= PAPER_EPD_MODE_QUALITY && mode <= PAPER_EPD_MODE_FASTEST) {
        s_epd_mode = mode;
    }
}

paper_epd_mode_t paper_epd_get_mode(void)
{
    return s_epd_mode;
}

esp_err_t paper_epd_init(void)
{
    if (s_epd_ready) {
        return ESP_OK;
    }

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PAPER_EPD_PIN_CS) | (1ULL << PAPER_EPD_PIN_DC) | (1ULL << PAPER_EPD_PIN_RST);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio out");
    gpio_set_level(PAPER_EPD_PIN_CS, 1);
    gpio_set_level(PAPER_EPD_PIN_DC, 1);

    gpio_config_t busy = {};
    busy.pin_bit_mask = (1ULL << PAPER_EPD_PIN_BUSY);
    busy.mode = GPIO_MODE_INPUT;
    busy.pull_up_en = GPIO_PULLUP_ENABLE;
    busy.pull_down_en = GPIO_PULLDOWN_DISABLE;
    busy.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&busy), TAG, "busy gpio");

    gpio_set_level(PAPER_EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PAPER_EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = PAPER_EPD_PIN_MOSI;
    bus_cfg.miso_io_num = PAPER_EPD_PIN_MISO;
    bus_cfg.sclk_io_num = PAPER_EPD_PIN_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = PAPER_EPD_ROW_BYTES;
    esp_err_t err = spi_bus_initialize(PAPER_EPD_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "spi bus");
    }

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = 4 * 1000 * 1000;
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = -1;
    dev_cfg.queue_size = 1;
    dev_cfg.flags = SPI_DEVICE_HALFDUPLEX;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(PAPER_EPD_HOST, &dev_cfg, &s_epd_spi), TAG, "spi dev");

    ESP_RETURN_ON_ERROR(epd_init_sequence(), TAG, "init seq");
    s_epd_ready = true;
    ESP_LOGI(TAG, "ED2208 display ready");
    return ESP_OK;
}

bool paper_epd_ready(void)
{
    return s_epd_ready;
}

esp_err_t paper_epd_smoke_test(void)
{
    ESP_RETURN_ON_ERROR(paper_epd_init(), TAG, "smoke init");
    uint8_t *row = (uint8_t *)heap_caps_malloc(PAPER_EPD_ROW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(row != NULL, ESP_ERR_NO_MEM, TAG, "smoke row");

    epd_select(true);
    esp_err_t err = epd_cmd(0x10);
    for (int py = 0; err == ESP_OK && py < PAPER_EPD_PANEL_H; ++py) {
        for (int px = 0; px < PAPER_EPD_PANEL_W; px += 2) {
            const bool border = px < 24 || px >= PAPER_EPD_PANEL_W - 24 ||
                                py < 24 || py >= PAPER_EPD_PANEL_H - 24;
            const bool stripe = py >= PAPER_EPD_PANEL_H / 2 - 16 && py < PAPER_EPD_PANEL_H / 2 + 16;
            const uint8_t c = (border || stripe) ? 0x3 : 0x1;
            row[px >> 1] = (uint8_t)((c << 4) | c);
        }
        err = epd_data(row, PAPER_EPD_ROW_BYTES);
    }
    epd_select(false);
    heap_caps_free(row);
    ESP_RETURN_ON_ERROR(err, TAG, "smoke image data");
    ESP_RETURN_ON_ERROR(epd_refresh(), TAG, "smoke refresh");
    ESP_LOGI(TAG, "ED2208 smoke refresh complete");
    return ESP_OK;
}

esp_err_t paper_epd_flush_rgb565(const uint16_t *fb, int logical_w, int logical_h)
{
    if (!s_epd_ready || fb == NULL || logical_w != PAPER_EPD_PANEL_W || logical_h != PAPER_EPD_PANEL_H) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *row = (uint8_t *)heap_caps_malloc(PAPER_EPD_ROW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(row != NULL, ESP_ERR_NO_MEM, TAG, "row");

    epd_select(true);
    esp_err_t err = epd_cmd(0x10);
    for (int py = 0; err == ESP_OK && py < PAPER_EPD_PANEL_H; ++py) {
        epd_convert_row_rgb565(&fb[py * logical_w], row, py, PAPER_EPD_PANEL_W);
        err = epd_data(row, PAPER_EPD_ROW_BYTES);
    }
    epd_select(false);
    heap_caps_free(row);
    ESP_RETURN_ON_ERROR(err, TAG, "image data");
    ESP_RETURN_ON_ERROR(epd_refresh(), TAG, "refresh");
    return ESP_OK;
}
