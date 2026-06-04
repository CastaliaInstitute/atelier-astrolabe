#include "faculty18_pmu.h"

#include "driver/i2c.h"
#include "esp_log.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "faculty_pmu";

static XPowersPMU s_pmu;

static int pmu_register_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if (len == 0 || data == NULL) {
        return -1;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return -1;
    }

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK ? 0 : -1;
}

static int pmu_register_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    const esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK ? 0 : -1;
}

extern "C" esp_err_t faculty18_pmu_init(void)
{
    if (!s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_register_read, pmu_register_write_byte)) {
        ESP_LOGE(TAG, "AXP2101 not found on I2C");
        return ESP_FAIL;
    }

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

    ESP_LOGI(TAG,
             "AXP2101 rails: DC1=%u DC3=%u BLDO1=%u BLDO2=%u mV",
             s_pmu.getDC1Voltage(),
             s_pmu.getDC3Voltage(),
             s_pmu.getBLDO1Voltage(),
             s_pmu.getBLDO2Voltage());
    return ESP_OK;
}
