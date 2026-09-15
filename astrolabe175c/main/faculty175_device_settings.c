#include "faculty175_device_settings.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "nvs.h"

#include "astrolabe_time.h"
#include "faculty175_util.h"

#define DEVICE_SETTINGS_NVS_NS "device_cfg"
#define DEVICE_SETTINGS_LOC_KEY "location"
#define DEVICE_SETTINGS_PERSONAL_KEY "personal"

typedef struct {
    uint32_t version;
    double lat_deg;
    double lon_deg;
    int64_t updated_epoch;
    char source[32];
    uint8_t valid;
} location_blob_t;

typedef struct {
    uint32_t version;
    faculty175_personal_settings_t settings;
} personal_blob_t;

bool faculty175_location_settings_valid(double lat_deg, double lon_deg)
{
    return isfinite(lat_deg) && isfinite(lon_deg) && lat_deg >= -90.0 && lat_deg <= 90.0 &&
           lon_deg >= -180.0 && lon_deg <= 180.0;
}

esp_err_t faculty175_location_settings_load(faculty175_location_settings_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    location_blob_t blob = {};
    size_t len = sizeof(blob);
    err = nvs_get_blob(nvs, DEVICE_SETTINGS_LOC_KEY, &blob, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(blob) || blob.version != 1 || !blob.valid ||
        !faculty175_location_settings_valid(blob.lat_deg, blob.lon_deg)) {
        return ESP_ERR_INVALID_STATE;
    }
    out->valid = true;
    out->lat_deg = blob.lat_deg;
    out->lon_deg = blob.lon_deg;
    out->updated_epoch = blob.updated_epoch;
    faculty175_strlcpy(out->source, blob.source, sizeof(out->source));
    return ESP_OK;
}

esp_err_t faculty175_location_settings_save(double lat_deg, double lon_deg, const char *source)
{
    if (!faculty175_location_settings_valid(lat_deg, lon_deg)) {
        return ESP_ERR_INVALID_ARG;
    }
    location_blob_t blob = {
        .version = 1,
        .lat_deg = lat_deg,
        .lon_deg = lon_deg,
        .updated_epoch = astrolabe_time_valid() ? (int64_t)astrolabe_time_now() : (int64_t)time(NULL),
        .valid = 1,
    };
    faculty175_strlcpy(blob.source, source != NULL && source[0] != '\0' ? source : "pwa", sizeof(blob.source));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, DEVICE_SETTINGS_LOC_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t faculty175_personal_settings_load(faculty175_personal_settings_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    personal_blob_t blob = {};
    size_t len = sizeof(blob);
    err = nvs_get_blob(nvs, DEVICE_SETTINGS_PERSONAL_KEY, &blob, &len);
    nvs_close(nvs);
    if (err != ESP_OK) return err;
    if (len != sizeof(blob) || blob.version != 1 || !blob.settings.valid) return ESP_ERR_INVALID_STATE;
    *out = blob.settings;
    return ESP_OK;
}

esp_err_t faculty175_personal_settings_save(const faculty175_personal_settings_t *settings)
{
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    personal_blob_t blob = {.version = 1, .settings = *settings};
    blob.settings.valid = true;
    blob.settings.display_name[sizeof(blob.settings.display_name) - 1] = '\0';
    blob.settings.pronouns[sizeof(blob.settings.pronouns) - 1] = '\0';
    blob.settings.birth_date[sizeof(blob.settings.birth_date) - 1] = '\0';
    blob.settings.birth_time[sizeof(blob.settings.birth_time) - 1] = '\0';
    blob.settings.birthplace[sizeof(blob.settings.birthplace) - 1] = '\0';
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, DEVICE_SETTINGS_PERSONAL_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
