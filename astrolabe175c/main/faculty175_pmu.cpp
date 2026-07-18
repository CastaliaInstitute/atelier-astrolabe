#include "faculty175_pmu.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "faculty175_board.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "faculty_pmu";

static XPowersPMU s_pmu;
static bool s_pmu_ready;

static constexpr int kPmuI2cTimeoutMs = 1000;

static int pmu_register_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    (void)kPmuI2cTimeoutMs;
    return faculty175_i2c_write_read(dev_addr, &reg_addr, 1, data, len) == ESP_OK ? 0 : -1;
}

static int pmu_register_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    uint8_t buf[16];
    if (len + 1u > sizeof(buf)) {
        return -1;
    }
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    (void)kPmuI2cTimeoutMs;
    return faculty175_i2c_write(dev_addr, buf, len + 1) == ESP_OK ? 0 : -1;
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

    /* Exact 1.75C schematic: DCDC1 is VCC3V3 and ALDO1 is A3V3 for the
       ES8311/ES7210 analog domains. The AMOLED connector also uses VCC3V3.
       ALDO2..4 and BLDO1..2 have no downstream consumers on this board. */
    s_pmu.setDC1Voltage(3300);
    s_pmu.enableDC1();
    s_pmu.setALDO1Voltage(3300);
    s_pmu.enableALDO1();

    s_pmu.disableTSPinMeasure();
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_LONG_IRQ);
}

extern "C" esp_err_t faculty175_pmu_init(void)
{
    /* Cold boot: give the AXP2101 and the shared I2C bus time to settle before
       hammering it — early transactions otherwise time out and the rails never
       get programmed (BLDO1 stays at 500 mV and the OLED never powers up). */
    vTaskDelay(pdMS_TO_TICKS(200));

    if (!faculty175_i2c_probe(AXP2101_SLAVE_ADDRESS)) {
        ESP_LOGE(TAG, "AXP2101 not found on I2C probe");
        return ESP_FAIL;
    }

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

    /* Cold-boot I2C writes can silently drop. Re-assert the populated audio
       analog rail without toggling the ESP32's DCDC1 supply. */
    uint16_t aldo1 = s_pmu.getALDO1Voltage();
    for (int attempt = 0; attempt < 12 && aldo1 < 3000; ++attempt) {
        ESP_LOGW(TAG, "AXP2101 ALDO1=%u mV — re-asserting A3V3 rail (%d)", aldo1, attempt + 1);
        s_pmu.setALDO1Voltage(3300);
        s_pmu.enableALDO1();
        vTaskDelay(pdMS_TO_TICKS(50));
        aldo1 = s_pmu.getALDO1Voltage();
    }

    const uint16_t dc1 = s_pmu.getDC1Voltage();
    ESP_LOGI(TAG, "AXP2101 populated rails: DC1/VCC3V3=%u ALDO1/A3V3=%u mV", dc1, aldo1);
    if (dc1 < 3000 || aldo1 < 3000) {
        ESP_LOGE(TAG, "required 3.3 V rail is low");
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
    out->power_on_source_flags = static_cast<uint8_t>(s_pmu.getPowerOnSource());
    out->power_off_source_flags = static_cast<uint8_t>(s_pmu.getPowerOffSource());
    return true;
}

extern "C" bool faculty175_pmu_pekey_long_press(void)
{
    if (!s_pmu_ready) {
        return false;
    }
    const uint64_t irq = s_pmu.getIrqStatus();
    const bool long_press = irq != 0 && s_pmu.isPekeyLongPressIrq();
    if (irq != 0) {
        s_pmu.clearIrqStatus();
    }
    return long_press;
}

extern "C" bool faculty175_pmu_prepare_deep_sleep(void)
{
    if (!s_pmu_ready) {
        return false;
    }
    /* ALDO1 supplies the audio-codec A3V3 net.  The codecs share SDA/SCL with
       the AXP2101 and clamp SDA low when that rail is removed, leaving the
       waking ESP32 unable to reach the PMU to restore ALDO1.  Keep the shared
       bus powered; the codec streams, I2S channels, and speaker PA have
       already been quiesced.  The remaining ALDO/BLDO outputs are unpopulated
       on the exact 1.75C schematic and can still be shut down. */
    const bool aldo1_on = s_pmu.enableALDO1();
    const bool aldo2_off = s_pmu.disableALDO2();
    const bool aldo3_off = s_pmu.disableALDO3();
    const bool aldo4_off = s_pmu.disableALDO4();
    const bool bldo1_off = s_pmu.disableBLDO1();
    const bool bldo2_off = s_pmu.disableBLDO2();
    ESP_LOGI(TAG,
             "deep sleep LDOs ALDO1=%s(shared-I2C) ALDO2=%s ALDO3=%s ALDO4=%s BLDO1=%s BLDO2=%s",
             aldo1_on ? "on" : "error",
             aldo2_off ? "off" : "error",
             aldo3_off ? "off" : "error",
             aldo4_off ? "off" : "error",
             bldo1_off ? "off" : "error",
             bldo2_off ? "off" : "error");
    return aldo1_on && aldo2_off && aldo3_off && aldo4_off && bldo1_off && bldo2_off;
}
