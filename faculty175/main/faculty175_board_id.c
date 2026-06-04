#include "faculty175_board_id.h"

#include <stdio.h>

#include <string.h>

#include "faculty175_log.h"
#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "sdkconfig.h"

#include "faculty175_board_id.h"
#include "faculty175_board.h"

static const char *TAG = "faculty_id";

static faculty175_board_identity_t s_id;

static bool i2c_has_device(uint8_t addr_7bit)
{
    i2c_master_bus_handle_t bus = faculty175_i2c_bus();
    if (bus == NULL) {
        return false;
    }
    return i2c_master_probe(bus, addr_7bit, 100) == ESP_OK;
}

static uint32_t probe_flash_mb(void)
{
    uint32_t size_bytes = 0;
    if (esp_flash_get_size(NULL, &size_bytes) != ESP_OK || size_bytes == 0) {
        return 0;
    }
    return (size_bytes + (1024u * 1024u - 1u)) / (1024u * 1024u);
}

static faculty175_board_guess_t guess_board(const faculty175_board_identity_t *id)
{
    /* ES7210 is present on 1.75/1.75C; 1.8″ uses TCA9554 instead (no ES7210). */
    if (id->es7210) {
        return id->flash_mb >= 32 ? FACULTY175_GUESS_175C : FACULTY175_GUESS_175;
    }
    if (id->tca9554) {
        return FACULTY175_GUESS_18_WRONG_FW;
    }
    if (id->es8311 && id->flash_mb > 0) {
        return id->flash_mb >= 32 ? FACULTY175_GUESS_175C : FACULTY175_GUESS_175;
    }
    return FACULTY175_GUESS_UNKNOWN;
}

static uint32_t configured_flash_mb(void)
{
#if CONFIG_ESPTOOLPY_FLASHSIZE_32MB
    return 32;
#elif CONFIG_ESPTOOLPY_FLASHSIZE_16MB
    return 16;
#elif CONFIG_ESPTOOLPY_FLASHSIZE_8MB
    return 8;
#elif CONFIG_ESPTOOLPY_FLASHSIZE_4MB
    return 4;
#else
    return 0;
#endif
}

const char *faculty175_board_guess_name(faculty175_board_guess_t guess)
{
    switch (guess) {
        case FACULTY175_GUESS_175C:
            return "ESP32-S3-Touch-AMOLED-1.75C";
        case FACULTY175_GUESS_175:
            return "ESP32-S3-Touch-AMOLED-1.75";
        case FACULTY175_GUESS_18_WRONG_FW:
            return "ESP32-S3-Touch-AMOLED-1.8 (wrong firmware)";
        default:
            return "unknown";
    }
}

const char *faculty175_board_recommended_project(void)
{
    switch (s_id.guess) {
        case FACULTY175_GUESS_18_WRONG_FW:
            return "faculty18";
        case FACULTY175_GUESS_175C:
        case FACULTY175_GUESS_175:
            return "faculty175";
        default:
            return "faculty175";
    }
}

const faculty175_board_identity_t *faculty175_board_identity(void)
{
    return &s_id;
}

void faculty175_board_log_identity(void)
{
    memset(&s_id, 0, sizeof(s_id));
    esp_read_mac(s_id.mac, ESP_MAC_WIFI_STA);

    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    s_id.psram_mb = (chip.features & CHIP_FEATURE_EMB_PSRAM) != 0 ? 8 : 0;

    s_id.flash_mb = probe_flash_mb();
    s_id.axp2101 = i2c_has_device(0x34);
    s_id.tca9554 = i2c_has_device(0x20);
    s_id.es7210 = i2c_has_device(0x40);
    s_id.es8311 = i2c_has_device(0x18);
    s_id.cst9217 = i2c_has_device(0x15);
    s_id.guess = guess_board(&s_id);

    const uint32_t cfg_mb = configured_flash_mb();
    s_id.flash_config_mismatch = cfg_mb > 0 && s_id.flash_mb > 0 && cfg_mb != s_id.flash_mb;

    ESP_LOGI(TAG,
             "MAC %02x:%02x:%02x:%02x:%02x:%02x flash=%uMB psram=%uMB cfg_flash=%uMB",
             s_id.mac[0],
             s_id.mac[1],
             s_id.mac[2],
             s_id.mac[3],
             s_id.mac[4],
             s_id.mac[5],
             (unsigned)s_id.flash_mb,
             (unsigned)s_id.psram_mb,
             (unsigned)cfg_mb);
    ESP_LOGI(TAG,
             "I2C AXP2101=%d TCA9554=%d ES7210=%d ES8311=%d CST9217=%d",
             s_id.axp2101,
             s_id.tca9554,
             s_id.es7210,
             s_id.es8311,
             s_id.cst9217);

    FACULTY175_LOG_STAGE(TAG,
                   "board",
                   "guess=%s project=%s",
                   faculty175_board_guess_name(s_id.guess),
                   faculty175_board_recommended_project());

    if (s_id.guess == FACULTY175_GUESS_18_WRONG_FW) {
        FACULTY175_LOG_STAGE_E(TAG, "board", "TCA9554 found — this is the 1.8″ board; flash faculty18 not faculty175");
    } else if (s_id.guess == FACULTY175_GUESS_175) {
        FACULTY175_LOG_STAGE_W(TAG, "board", "16 MB flash + ES7210 → 1.75″ (non-C); LCD reset uses GPIO 2");
    } else if (s_id.guess == FACULTY175_GUESS_175C) {
        FACULTY175_LOG_STAGE(TAG, "board", "32 MB flash + ES7210 → 1.75C; LCD reset uses GPIO 1");
    }

    if (s_id.flash_config_mismatch) {
        FACULTY175_LOG_STAGE_E(TAG,
                         "board",
                         "sdkconfig flash=%uMB but chip=%uMB — rebuild with matching CONFIG_ESPTOOLPY_FLASHSIZE",
                         (unsigned)cfg_mb,
                         (unsigned)s_id.flash_mb);
    }
}
