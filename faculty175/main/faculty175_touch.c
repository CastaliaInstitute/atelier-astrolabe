#include "faculty175_touch.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"
#include "faculty175_board_id.h"

static const char *TAG = "faculty_touch";

#define CST9217_I2C_ADDR 0x15
#define CST9217_REG_DATA 0xD000
#define CST9217_REG_CMD_MODE 0xD101
#define CST9217_ACK 0xAB
#define CST9217_MAX_POINTS 1
#define CST9217_DATA_LEN 10

#define FACULTY175_TP_INT GPIO_NUM_11
#define FACULTY175_TP_RST GPIO_NUM_2

static i2c_master_dev_handle_t s_touch_dev;
static bool s_touch_ok;

static esp_err_t cst9217_bus_read(uint16_t reg, uint8_t *data, size_t len)
{
    if (s_touch_dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t reg_be[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xff)};
    return i2c_master_transmit_receive(s_touch_dev, reg_be, sizeof(reg_be), data, len, 50);
}

static esp_err_t cst9217_bus_write(uint16_t reg, const uint8_t *data, size_t len)
{
    if (s_touch_dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[8];
    if (len + 2 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xff);
    memcpy(buf + 2, data, len);
    return i2c_master_transmit(s_touch_dev, buf, len + 2, 50);
}

static void cst9217_reset(void)
{
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << FACULTY175_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    (void)gpio_config(&rst);
    (void)gpio_set_level(FACULTY175_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    (void)gpio_set_level(FACULTY175_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t cst9217_read_config(void)
{
    const uint8_t cmd_mode[] = {0xD1, 0x01};
    if (cst9217_bus_write(CST9217_REG_CMD_MODE, cmd_mode, sizeof(cmd_mode)) != ESP_OK) {
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t res[4] = {0};
    if (cst9217_bus_read(0xD1F8, res, sizeof(res)) != ESP_OK) {
        return ESP_FAIL;
    }
    const uint16_t res_x = (uint16_t)res[1] << 8 | res[0];
    const uint16_t res_y = (uint16_t)res[3] << 8 | res[2];
    ESP_LOGI(TAG, "CST9217 touch %ux%u", (unsigned)res_x, (unsigned)res_y);
    return ESP_OK;
}

esp_err_t faculty175_touch_init(void)
{
    const faculty175_board_identity_t *id = faculty175_board_identity();
    if (id == NULL || !id->cst9217) {
        ESP_LOGI(TAG, "CST9217 not detected — touch disabled");
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = faculty175_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cst9217_reset();

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CST9217_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_touch_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch I2C add failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t intr = {
        .pin_bit_mask = 1ULL << FACULTY175_TP_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&intr);

    (void)cst9217_read_config();

    s_touch_ok = true;
    ESP_LOGI(TAG, "CST9217 touch ok (INT=%d RST=%d)", (int)FACULTY175_TP_INT, (int)FACULTY175_TP_RST);
    return ESP_OK;
}

bool faculty175_touch_ready(void)
{
    return s_touch_ok && s_touch_dev != NULL;
}

uint8_t faculty175_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts)
{
    if (!faculty175_touch_ready() || xs == NULL || ys == NULL || max_pts == 0) {
        return 0;
    }

    uint8_t data[CST9217_DATA_LEN] = {0};
    if (cst9217_bus_read(CST9217_REG_DATA, data, sizeof(data)) != ESP_OK) {
        return 0;
    }

    const uint8_t ack = CST9217_ACK;
    (void)cst9217_bus_write(CST9217_REG_DATA, &ack, 1);

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
