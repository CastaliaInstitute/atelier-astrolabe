#include "faculty175_ring.h"

#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"

static faculty175_ring_vitals_t s_latest;
static bool s_have_latest;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static const cJSON *json_any(const cJSON *root, const char *a, const char *b, const char *c)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, a);
    if (item == NULL && b != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(root, b);
    }
    if (item == NULL && c != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(root, c);
    }
    return item;
}

static bool json_u16(const cJSON *item, uint16_t min, uint16_t max, uint16_t *out)
{
    if (!cJSON_IsNumber(item) || out == NULL) {
        return false;
    }
    const int value = item->valueint;
    if (value < (int)min || value > (int)max) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

void faculty175_ring_update_vitals(const faculty175_ring_vitals_t *vitals)
{
    if (vitals == NULL) {
        return;
    }
    s_latest = *vitals;
    if (s_latest.updated_ms == 0) {
        s_latest.updated_ms = now_ms();
    }
    s_have_latest = s_latest.heart_rate_valid || s_latest.hrv_valid || s_latest.spo2_valid || s_latest.battery_valid;
}

bool faculty175_ring_latest_vitals(faculty175_ring_vitals_t *out)
{
    if (out == NULL || !s_have_latest) {
        return false;
    }
    *out = s_latest;
    return true;
}

esp_err_t faculty175_ring_handle_json(const char *json)
{
    if (json == NULL || json[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    faculty175_ring_vitals_t vitals = {};
    uint16_t value = 0;
    const cJSON *hr = json_any(root, "heartRate", "heartRateBpm", "bpm");
    if (json_u16(hr, 30, 240, &value)) {
        vitals.heart_rate_bpm = value;
        vitals.heart_rate_valid = true;
    }
    const cJSON *hrv = json_any(root, "hrv", "hrvMs", "rmssd");
    if (json_u16(hrv, 1, 500, &value)) {
        vitals.hrv_ms = value;
        vitals.hrv_valid = true;
    }
    const cJSON *spo2 = json_any(root, "spo2", "spo2Percent", "bloodOxygen");
    if (json_u16(spo2, 50, 100, &value)) {
        vitals.spo2_percent = (uint8_t)value;
        vitals.spo2_valid = true;
    }
    const cJSON *battery = json_any(root, "battery", "batteryPercent", NULL);
    if (json_u16(battery, 0, 100, &value)) {
        vitals.battery_percent = (uint8_t)value;
        vitals.battery_valid = true;
    }

    const cJSON *updated = json_any(root, "updatedMs", "updated_at_ms", NULL);
    if (cJSON_IsNumber(updated) && updated->valuedouble > 0.0) {
        vitals.updated_ms = (uint64_t)updated->valuedouble;
    }

    cJSON_Delete(root);
    if (!vitals.heart_rate_valid && !vitals.hrv_valid && !vitals.spo2_valid && !vitals.battery_valid) {
        return ESP_ERR_NOT_FOUND;
    }
    faculty175_ring_update_vitals(&vitals);
    return ESP_OK;
}
