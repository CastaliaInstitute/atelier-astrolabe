#include "faculty175_touch.h"

#include "driver/gpio.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"
#include "faculty175_board_id.h"

static const char *TAG = "faculty_touch";

#define FACULTY175_TP_INT GPIO_NUM_11
#define FACULTY175_TP_RST GPIO_NUM_2
#define FACULTY175_TP_RST_SHARED_WITH_LCD 1
#define FACULTY175_TP_I2C_HZ 100000
#define FACULTY175_TP_INIT_ATTEMPTS 3
#define FACULTY175_TP_PROBE_TIMEOUT_MS 25
#define CST9217_REG_DATA 0xD000
#define CST9217_ACK 0xAB
#define CST9217_MAX_POINTS 2
#define CST9217_DATA_LEN (CST9217_MAX_POINTS * 5 + 5)

static bool s_touch_ok;
static volatile bool s_touch_irq_latched;
static bool s_touch_active;
static portMUX_TYPE s_touch_state_mux = portMUX_INITIALIZER_UNLOCKED;
static faculty175_touch_state_t s_touch_state;
static faculty175_touch_diagnostics_t s_touch_diag;

static void IRAM_ATTR touch_int_isr(void *arg)
{
    (void)arg;
    s_touch_diag.irq_count++;
    s_touch_irq_latched = true;
}

static void cst9217_reset_pulse(void)
{
#if FACULTY175_TP_RST_SHARED_WITH_LCD
    const gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << FACULTY175_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&rst_cfg);
    (void)gpio_set_level(FACULTY175_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(80));
    return;
#else
    const gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << FACULTY175_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&rst_cfg);
    (void)gpio_set_level(FACULTY175_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    (void)gpio_set_level(FACULTY175_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(80));
#endif
}

static esp_err_t cst9217_panel_io_read(uint16_t reg, uint8_t *data, size_t len)
{
    if (!s_touch_ok || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg_buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xff)};
    return faculty175_i2c_write_read(ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS, reg_buf, sizeof(reg_buf), data, len);
}

static void cst9217_ack_data_frame(void)
{
    const uint8_t ack[3] = {
        (uint8_t)(CST9217_REG_DATA >> 8),
        (uint8_t)(CST9217_REG_DATA & 0xff),
        CST9217_ACK,
    };
    (void)faculty175_i2c_write(ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS, ack, sizeof(ack));
}

esp_err_t faculty175_touch_init(void)
{
    if (s_touch_ok) {
        return ESP_OK;
    }

    const faculty175_board_identity_t *id = faculty175_board_identity();
    if (id != NULL && id->guess == FACULTY175_GUESS_18_WRONG_FW) {
        ESP_LOGI(TAG, "wrong board for CST9217 touch - disabled");
        return ESP_OK;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= FACULTY175_TP_INIT_ATTEMPTS; ++attempt) {
        cst9217_reset_pulse();

        (void)FACULTY175_TP_PROBE_TIMEOUT_MS;
        if (!faculty175_i2c_probe(ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS)) {
            ESP_LOGW(TAG, "CST9217 probe failed attempt=%d addr=0x%02x: %s",
                     attempt,
                     ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS,
                     esp_err_to_name(ESP_FAIL));
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }
        err = ESP_OK;
        break;
    }

    if (err != ESP_OK) {
        s_touch_diag.init_err = err;
        ESP_LOGW(TAG, "CST9217 touch unavailable after %d attempts", FACULTY175_TP_INIT_ATTEMPTS);
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
    s_touch_diag.init_err = ESP_OK;
    s_touch_diag.ready = true;
    ESP_LOGI(TAG, "CST9217 touch ok addr=0x%02x early_probe=%d INT=%d RST=%d",
             ESP_LCD_TOUCH_IO_I2C_CST9217_ADDRESS,
             id != NULL ? id->cst9217 : 0,
             (int)FACULTY175_TP_INT,
             (int)FACULTY175_TP_RST);
    return ESP_OK;
}

bool faculty175_touch_ready(void)
{
    return s_touch_ok;
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
    if (!s_touch_active && gpio_get_level(FACULTY175_TP_INT) != 0 && !irq_latched) {
        return 0;
    }
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    s_touch_irq_latched = false;
    s_touch_diag.read_count++;

    uint8_t data[CST9217_DATA_LEN] = {0};
    if (cst9217_panel_io_read(CST9217_REG_DATA, data, sizeof(data)) != ESP_OK) {
        s_touch_diag.read_errors++;
        s_touch_active = false;
        return 0;
    }
    uint8_t points = data[5] & 0x7f;
    if (points <= CST9217_MAX_POINTS) {
        s_touch_diag.last_frame_ms = now_ms;
        s_touch_diag.last_points = points;
        s_touch_diag.last_data0 = data[0];
        s_touch_diag.last_data6 = data[6];
        s_touch_diag.last_event = data[0] & 0x0f;
        if (points > 0) {
            s_touch_diag.raw_x = (int16_t)(((uint16_t)data[1] << 4) | (data[3] >> 4));
            s_touch_diag.raw_y = (int16_t)(((uint16_t)data[2] << 4) | (data[3] & 0x0f));
        }
    }
    /*
     * Match the vendor transaction exactly: every read triggered by IRQ (or
     * while tracking an active contact) is acknowledged before validation.
     */
    cst9217_ack_data_frame();
    if (data[6] != CST9217_ACK || points > CST9217_MAX_POINTS) {
        s_touch_diag.invalid_frames++;
        s_touch_active = false;
        return 0;
    }

    uint8_t out_n = 0;
    for (uint8_t i = 0; i < points && out_n < max_pts; ++i) {
        const uint8_t *p = &data[i * 5 + (i ? 2 : 0)];
        const uint8_t event = p[0] & 0x0f;
        s_touch_diag.last_event = event;
        if (event != 0x06) {
            s_touch_diag.rejected_events++;
            continue;
        }
        const int16_t raw_x = (int16_t)(((uint16_t)p[1] << 4) | (p[3] >> 4));
        const int16_t raw_y = (int16_t)(((uint16_t)p[2] << 4) | (p[3] & 0x0f));
        s_touch_diag.raw_x = raw_x;
        s_touch_diag.raw_y = raw_y;
        if (raw_x < 0 || raw_x >= FACULTY175_LCD_W || raw_y < 0 || raw_y >= FACULTY175_LCD_H) {
            s_touch_diag.invalid_frames++;
            continue;
        }
        /* CST9217 axes increase opposite the CO5300 panel's visible axes on
         * the 1.75C. Normalize them here so every consumer—gesture classifier,
         * LVGL pointer, bezel hit testing, and touch indicator—shares one
         * physical coordinate system. */
        const int16_t x = (int16_t)(FACULTY175_LCD_W - 1 - raw_x);
        const int16_t y = (int16_t)(FACULTY175_LCD_H - 1 - raw_y);
        xs[out_n] = x;
        ys[out_n] = y;
        s_touch_diag.x = x;
        s_touch_diag.y = y;
        s_touch_diag.valid_points++;
        ++out_n;
    }
    s_touch_active = out_n > 0;
    s_touch_diag.active = s_touch_active;
    return out_n;
}

void faculty175_touch_state_update(bool down, int16_t x, int16_t y, uint32_t now_ms)
{
    portENTER_CRITICAL(&s_touch_state_mux);
    s_touch_state.down = down;
    s_touch_state.x = x;
    s_touch_state.y = y;
    s_touch_state.updated_ms = now_ms;
    s_touch_diag.down = down;
    s_touch_diag.updated_ms = now_ms;
    s_touch_diag.x = x;
    s_touch_diag.y = y;
    portEXIT_CRITICAL(&s_touch_state_mux);
}

faculty175_touch_state_t faculty175_touch_state_get(void)
{
    faculty175_touch_state_t state;
    portENTER_CRITICAL(&s_touch_state_mux);
    state = s_touch_state;
    portEXIT_CRITICAL(&s_touch_state_mux);
    return state;
}

void faculty175_touch_diagnostics_get(faculty175_touch_diagnostics_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_touch_state_mux);
    *out = s_touch_diag;
    out->ready = s_touch_ok;
    out->int_active = s_touch_ok && (gpio_get_level(FACULTY175_TP_INT) == 0 || s_touch_irq_latched);
    out->active = s_touch_active;
    portEXIT_CRITICAL(&s_touch_state_mux);
}
