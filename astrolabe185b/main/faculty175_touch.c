#include "faculty175_touch.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"

#include "faculty175_board.h"

static const char *TAG = "faculty_touch";

#define FACULTY175_TP_INT GPIO_NUM_4
#define FACULTY175_TP_RST GPIO_NUM_1
#define FACULTY175_TP_I2C_HZ 100000

static bool s_touch_ok;
static esp_lcd_touch_handle_t s_touch;
static portMUX_TYPE s_touch_state_mux = portMUX_INITIALIZER_UNLOCKED;
static faculty175_touch_state_t s_touch_state;

esp_err_t faculty175_touch_init(void)
{
    if (s_touch_ok) {
        return ESP_OK;
    }

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    tp_io_config.scl_speed_hz = FACULTY175_TP_I2C_HZ;

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_err_t err = esp_lcd_new_panel_io_i2c(faculty175_i2c_bus(), &tp_io_config, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST816S panel IO unavailable: %s", esp_err_to_name(err));
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
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };

    err = esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST816S touch unavailable: %s", esp_err_to_name(err));
        (void)esp_lcd_panel_io_del(tp_io);
        return ESP_OK;
    }

    s_touch_ok = true;
    ESP_LOGI(TAG, "CST816S touch ok INT=%d RST=%d", (int)FACULTY175_TP_INT, (int)FACULTY175_TP_RST);
    return ESP_OK;
}

bool faculty175_touch_ready(void)
{
    return s_touch_ok && s_touch != NULL;
}

bool faculty175_touch_int_active(void)
{
    return faculty175_touch_ready() && gpio_get_level(FACULTY175_TP_INT) == 0;
}

uint8_t faculty175_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts)
{
    if (!faculty175_touch_ready() || xs == NULL || ys == NULL || max_pts == 0) {
        return 0;
    }

    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return 0;
    }

    uint16_t tx[1] = {0};
    uint16_t ty[1] = {0};
    uint8_t points = 0;
    if (!esp_lcd_touch_get_coordinates(s_touch, tx, ty, NULL, &points, 1) || points == 0) {
        return 0;
    }

    xs[0] = (int16_t)tx[0];
    ys[0] = (int16_t)ty[0];
    return 1;
}

void faculty175_touch_state_update(bool down, int16_t x, int16_t y, uint32_t now_ms)
{
    portENTER_CRITICAL(&s_touch_state_mux);
    s_touch_state.down = down;
    s_touch_state.x = x;
    s_touch_state.y = y;
    s_touch_state.updated_ms = now_ms;
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
