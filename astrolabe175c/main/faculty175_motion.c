#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"
#include "faculty175_motion.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define QMI8658_ADDR_PRIMARY 0x6B
#define QMI8658_ADDR_ALT 0x6A
#define QMI8658_REG_WHO_AM_I 0x00
#define QMI8658_REG_CTRL1 0x02
#define QMI8658_REG_CTRL2 0x03
#define QMI8658_REG_CTRL7 0x08
#define QMI8658_REG_AX_L 0x35
#define QMI8658_WHO_AM_I_VALUE 0x05
#define QMI8658_CTRL1_ADDR_AI 0x40
#define QMI8658_CTRL2_ACC_2G_125HZ 0x06
#define QMI8658_CTRL7_ACC_ENABLE 0x01
#define QMI8658_ACC_2G_LSB_PER_G 16384.0f

static const char *TAG = "faculty175_motion";

static uint8_t s_qmi_addr;
static bool s_init_done;
static bool s_ready;
static bool s_logged_missing;

static bool qmi_read(uint8_t reg, uint8_t *out, size_t len)
{
    return s_qmi_addr != 0 && out != NULL &&
           faculty175_i2c_write_read(s_qmi_addr, &reg, 1, out, len) == ESP_OK;
}

static bool qmi_write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return s_qmi_addr != 0 && faculty175_i2c_write(s_qmi_addr, buf, sizeof(buf)) == ESP_OK;
}

static bool qmi_probe_addr(uint8_t addr)
{
    s_qmi_addr = addr;
    uint8_t who = 0;
    return qmi_read(QMI8658_REG_WHO_AM_I, &who, 1) && who == QMI8658_WHO_AM_I_VALUE;
}

static bool qmi_init_once(void)
{
    if (s_init_done) {
        return s_ready;
    }
    s_init_done = true;

    if (!qmi_probe_addr(QMI8658_ADDR_PRIMARY) && !qmi_probe_addr(QMI8658_ADDR_ALT)) {
        s_qmi_addr = 0;
        if (!s_logged_missing) {
            s_logged_missing = true;
            ESP_LOGW(TAG, "QMI8658 not detected");
        }
        return false;
    }

    if (!qmi_write_reg(QMI8658_REG_CTRL1, QMI8658_CTRL1_ADDR_AI) ||
        !qmi_write_reg(QMI8658_REG_CTRL2, QMI8658_CTRL2_ACC_2G_125HZ) ||
        !qmi_write_reg(QMI8658_REG_CTRL7, QMI8658_CTRL7_ACC_ENABLE)) {
        ESP_LOGW(TAG, "QMI8658 init failed at 0x%02x", s_qmi_addr);
        s_qmi_addr = 0;
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
    s_ready = true;
    ESP_LOGI(TAG, "QMI8658 accel ready at 0x%02x", s_qmi_addr);
    return true;
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

bool faculty175_motion_accel_g(float *ax_g, float *ay_g, float *az_g)
{
    if (ax_g == NULL || ay_g == NULL || az_g == NULL || !qmi_init_once()) {
        return false;
    }

    uint8_t raw[6];
    if (!qmi_read(QMI8658_REG_AX_L, raw, sizeof(raw))) {
        return false;
    }

    *ax_g = (float)le16(&raw[0]) / QMI8658_ACC_2G_LSB_PER_G;
    *ay_g = (float)le16(&raw[2]) / QMI8658_ACC_2G_LSB_PER_G;
    *az_g = (float)le16(&raw[4]) / QMI8658_ACC_2G_LSB_PER_G;
    return true;
}

bool faculty175_motion_pitch_roll(float *pitch_deg, float *roll_deg)
{
    if (pitch_deg == NULL || roll_deg == NULL) {
        return false;
    }

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (!faculty175_motion_accel_g(&ax, &ay, &az)) {
        return false;
    }

    const float horiz = sqrtf((ay * ay) + (az * az));
    *pitch_deg = atan2f(-ax, horiz) * 180.0f / (float)M_PI;
    *roll_deg = atan2f(ay, az) * 180.0f / (float)M_PI;
    return true;
}
