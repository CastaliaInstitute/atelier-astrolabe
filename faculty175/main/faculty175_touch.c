#include "faculty175_touch.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"

#include "faculty175_board.h"
#include "faculty175_board_id.h"

static const char *TAG = "faculty_touch";

#define FACULTY175_TP_INT GPIO_NUM_11
#define FACULTY175_TP_RST GPIO_NUM_2
#define FACULTY175_TP_I2C_HZ 400000
#define CST9217_REG_DATA 0xD000
#define CST9217_ACK 0xAB
#define CST9217_MAX_POINTS 1
#define CST9217_DATA_LEN 10

static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch;
static bool s_touch_ok;
static volatile bool s_touch_irq_latched;

static void IRAM_ATTR touch_int_isr(void *arg)
{
    (void)arg;
    s_touch_irq_latched = true;
}

static esp_err_t cst9217_panel_io_read(uint16_t reg, uint8_t *data, size_t len)
{
    if (s_touch_io == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg_lo = (uint8_t)(reg & 0xff);
    esp_err_t err = esp_lcd_panel_io_tx_param(s_touch_io, (int)(reg >> 8), &reg_lo, 1);
    if (err != ESP_OK) {
        return err;
    }
    return esp_lcd_panel_io_rx_param(s_touch_io, -1, data, len);
}

esp_err_t faculty175_touch_init(void)
{
    const faculty175_board_identity_t *id = faculty175_board_identity();
    if (id != NULL && id->guess == FACULTY175_GUESS_18_WRONG_FW) {
        ESP_LOGI(TAG, "wrong board for CST9217 touch - disabled");
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = faculty175_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = FACULTY175_TP_I2C_HZ;
    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &s_touch_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST9217 panel I2C IO failed addr=0x%02x: %s",
                 ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS,
                 esp_err_to_name(err));
        s_touch_io = NULL;
        return ESP_OK;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = FACULTY175_LCD_W,
        .y_max = FACULTY175_LCD_H,
        .rst_gpio_num = FACULTY175_TP_RST,
        .int_gpio_num = FACULTY175_TP_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    err = esp_lcd_touch_new_i2c_cst9217(s_touch_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST9217 touch init failed addr=0x%02x INT=%d RST=%d: %s",
                 ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS,
                 (int)FACULTY175_TP_INT,
                 (int)FACULTY175_TP_RST,
                 esp_err_to_name(err));
        (void)esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = NULL;
        s_touch = NULL;
        return ESP_OK;
    }

    const gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << FACULTY175_TP_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    (void)gpio_config(&int_cfg);
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err == ESP_OK || isr_err == ESP_ERR_INVALID_STATE) {
        (void)gpio_isr_handler_remove(FACULTY175_TP_INT);
        (void)gpio_isr_handler_add(FACULTY175_TP_INT, touch_int_isr, NULL);
    } else {
        ESP_LOGW(TAG, "touch ISR service unavailable: %s", esp_err_to_name(isr_err));
    }

    s_touch_ok = true;
    ESP_LOGI(TAG, "CST9217 touch ok addr=0x%02x early_probe=%d INT=%d RST=%d",
             ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS,
             id != NULL ? id->cst9217 : 0,
             (int)FACULTY175_TP_INT,
             (int)FACULTY175_TP_RST);
    return ESP_OK;
}

bool faculty175_touch_ready(void)
{
    return s_touch_ok && s_touch != NULL;
}

bool faculty175_touch_int_active(void)
{
    return faculty175_touch_ready() && (gpio_get_level(FACULTY175_TP_INT) == 0 || s_touch_irq_latched);
}

uint8_t faculty175_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts)
{
    if (!faculty175_touch_ready() || xs == NULL || ys == NULL || max_pts == 0) {
        return 0;
    }
    const bool irq_latched = s_touch_irq_latched;
    if (gpio_get_level(FACULTY175_TP_INT) != 0 && !irq_latched) {
        return 0;
    }
    s_touch_irq_latched = false;

    uint8_t data[CST9217_DATA_LEN] = {0};
    if (cst9217_panel_io_read(CST9217_REG_DATA, data, sizeof(data)) != ESP_OK) {
        return 0;
    }

    if (data[6] != CST9217_ACK) {
        return 0;
    }

    uint8_t points = data[5] & 0x7f;
    if (points > CST9217_MAX_POINTS) {
        points = CST9217_MAX_POINTS;
    }

    uint8_t out_n = 0;
    for (uint8_t i = 0; i < points && out_n < max_pts; ++i) {
        const uint8_t *p = &data[i * 5 + (i ? 2 : 0)];
        const uint8_t event = p[0] & 0x0f;
        if (event != 0x06) {
            continue;
        }
        const int16_t x = (int16_t)(((uint16_t)p[1] << 4) | (p[3] >> 4));
        const int16_t y = (int16_t)(((uint16_t)p[2] << 4) | (p[3] & 0x0f));
        if (x < 0 || x >= FACULTY175_LCD_W || y < 0 || y >= FACULTY175_LCD_H) {
            continue;
        }
        xs[out_n] = x;
        ys[out_n] = y;
        ++out_n;
    }
    return out_n;
}
