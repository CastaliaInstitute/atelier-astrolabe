#include "faculty175_pmu.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "faculty175_board.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "faculty_pmu";

static XPowersPMU s_pmu;
static i2c_master_dev_handle_t s_pmu_i2c;
static bool s_pmu_ready;

static esp_err_t pmu_i2c_dev(void)
{
    if (s_pmu_i2c != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = faculty175_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_SLAVE_ADDRESS,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_pmu_i2c);
}

static int pmu_register_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    (void)dev_addr;
    if (data == NULL || len == 0 || pmu_i2c_dev() != ESP_OK) {
        return -1;
    }
    return i2c_master_transmit_receive(s_pmu_i2c, &reg_addr, 1, data, len, 1000) == ESP_OK ? 0 : -1;
}

static int pmu_register_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    (void)dev_addr;
    if (data == NULL || len == 0 || pmu_i2c_dev() != ESP_OK) {
        return -1;
    }
    uint8_t buf[16];
    if (len + 1u > sizeof(buf)) {
        return -1;
    }
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_pmu_i2c, buf, len + 1, 1000) == ESP_OK ? 0 : -1;
}

static void faculty175_pmu_apply_rails(void)
{
    s_pmu.disableDC2();
    s_pmu.disableDC3();
    s_pmu.disableDC4();
    s_pmu.disableDC5();
    s_pmu.disableALDO1();
    s_pmu.disableALDO2();
    s_pmu.disableALDO3();
    s_pmu.disableALDO4();
    s_pmu.disableBLDO1();
    s_pmu.disableBLDO2();
    s_pmu.disableCPUSLDO();
    s_pmu.disableDLDO1();
    s_pmu.disableDLDO2();

    s_pmu.setDC3Voltage(3300);
    s_pmu.enableDC3();
    s_pmu.setDC1Voltage(3300);
    s_pmu.enableDC1();
    s_pmu.setALDO1Voltage(1800);
    s_pmu.enableALDO1();
    s_pmu.setALDO2Voltage(2800);
    s_pmu.enableALDO2();
    s_pmu.setALDO4Voltage(3000);
    s_pmu.enableALDO4();
    s_pmu.setALDO3Voltage(3300);
    s_pmu.enableALDO3();
    s_pmu.setBLDO1Voltage(3300);
    s_pmu.enableBLDO1();
    s_pmu.setBLDO2Voltage(3300);
    s_pmu.enableBLDO2();

    s_pmu.disableTSPinMeasure();
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
}

extern "C" esp_err_t faculty175_pmu_init(void)
{
    /* Cold boot: give the AXP2101 and the shared I2C bus time to settle before
       hammering it — early transactions otherwise time out and the rails never
       get programmed (BLDO1 stays at 500 mV and the OLED never powers up). */
    vTaskDelay(pdMS_TO_TICKS(200));

    bool begun = false;
    for (int attempt = 0; attempt < 10 && !begun; ++attempt) {
        begun = s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_register_read, pmu_register_write_byte);
        if (!begun) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    if (!begun) {
        ESP_LOGE(TAG, "AXP2101 not found on I2C after retries");
        return ESP_FAIL;
    }

    /* Program every rail once. Toggling the full sequence repeatedly browns out
       the USB rail and forces re-enumeration, so do it a single time. */
    faculty175_pmu_apply_rails();
    vTaskDelay(pdMS_TO_TICKS(60));

    /* Cold-boot I2C writes can silently drop — gently re-assert just the OLED
       rail (BLDO1) until it reads back at voltage, without touching core rails. */
    uint16_t bldo1 = s_pmu.getBLDO1Voltage();
    for (int attempt = 0; attempt < 12 && bldo1 < 3000; ++attempt) {
        ESP_LOGW(TAG, "AXP2101 BLDO1=%u mV — re-asserting OLED rail (%d)", bldo1, attempt + 1);
        s_pmu.setBLDO1Voltage(3300);
        s_pmu.enableBLDO1();
        vTaskDelay(pdMS_TO_TICKS(50));
        bldo1 = s_pmu.getBLDO1Voltage();
    }

    const uint16_t dc1 = s_pmu.getDC1Voltage();
    const uint16_t dc3 = s_pmu.getDC3Voltage();
    const uint16_t bldo2 = s_pmu.getBLDO2Voltage();
    ESP_LOGI(TAG, "AXP2101 rails: DC1=%u DC3=%u BLDO1=%u BLDO2=%u mV", dc1, dc3, bldo1, bldo2);
    if (bldo1 < 3000) {
        ESP_LOGE(TAG, "BLDO1 still low — OLED rail off, display will be dark");
        return ESP_FAIL;
    }
    s_pmu_ready = true;
    return ESP_OK;
}

extern "C" bool faculty175_pmu_status(faculty175_pmu_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = {};
    out->battery_percent = -1;
    if (!s_pmu_ready) {
        return false;
    }
    out->present = true;
    out->battery_present = s_pmu.isBatteryConnect();
    out->vbus_in = s_pmu.isVbusIn();
    out->charging = s_pmu.isCharging();
    out->discharging = s_pmu.isDischarge();
    out->battery_percent = s_pmu.getBatteryPercent();
    out->battery_mv = s_pmu.getBattVoltage();
    return true;
}
