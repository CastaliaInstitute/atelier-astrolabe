#include "faculty175_variant.h"

#include <strings.h>

#include "nvs.h"

#include "faculty175_log.h"

static const char *TAG = "faculty175_variant";

#define VARIANT_NVS_NS "mynah"
#define VARIANT_NVS_KEY "variant"
#define VARIANT_NVS_FW_KEY "fw_variant"
#define VARIANT_NVS_PLATFORM_KEY "device_platform"
#define VARIANT_NVS_OTA_KEY "ota_channel"

#ifndef ASTROLABE_DEVICE_PLATFORM
#define ASTROLABE_DEVICE_PLATFORM "1.75"
#endif

static bool variant_valid(uint8_t value)
{
    return value < (uint8_t)FACULTY175_VARIANT_COUNT;
}

static bool forced_profile(faculty175_face_profile_t *out)
{
#if defined(ASTROLABE_FORCE_VARIANT_ASTROLABE)
    *out = FACULTY175_FACE_PROFILE_CASTALIA;
    return true;
#elif defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
    *out = FACULTY175_FACE_PROFILE_LUNASAY;
    return true;
#elif defined(ASTROLABE_FORCE_VARIANT_OCARINA)
    *out = FACULTY175_FACE_PROFILE_OCARINA;
    return true;
#elif defined(ASTROLABE_FORCE_VARIANT_CAMEO)
    *out = FACULTY175_FACE_PROFILE_CAMEO;
    return true;
#elif defined(ASTROLABE_FORCE_VARIANT_CYBER)
    *out = FACULTY175_FACE_PROFILE_CYBER;
    return true;
#elif defined(ASTROLABE_FORCE_VARIANT_LUOPAN) || defined(ASTROLABE_FORCE_VARIANT_ENSO) || \
    defined(ASTROLABE_FORCE_VARIANT_SMART_SPEAKER) || defined(ASTROLABE_FORCE_VARIANT_BABEL_FISH) || \
    defined(ASTROLABE_FORCE_VARIANT_ELECROW_128)
    *out = FACULTY175_FACE_PROFILE_DEFAULT;
    return true;
#else
    (void)out;
    return false;
#endif
}

bool faculty175_variant_forced_profile(faculty175_face_profile_t *out)
{
    return out != NULL && forced_profile(out);
}

static faculty175_face_profile_t default_profile(void)
{
#if defined(ASTROLABE_DEFAULT_VARIANT_ASTROLABE)
    return FACULTY175_FACE_PROFILE_CASTALIA;
#elif defined(ASTROLABE_DEFAULT_VARIANT_LUNASAY)
    return FACULTY175_FACE_PROFILE_LUNASAY;
#elif defined(ASTROLABE_DEFAULT_VARIANT_OCARINA)
    return FACULTY175_FACE_PROFILE_OCARINA;
#elif defined(ASTROLABE_DEFAULT_VARIANT_CAMEO)
    return FACULTY175_FACE_PROFILE_CAMEO;
#elif defined(ASTROLABE_DEFAULT_VARIANT_CYBER)
    return FACULTY175_FACE_PROFILE_CYBER;
#elif defined(ASTROLABE_DEFAULT_VARIANT_ELECROW_128)
    return FACULTY175_FACE_PROFILE_CASTALIA;
#else
    return FACULTY175_FACE_PROFILE_DEFAULT;
#endif
}

static bool profile_from_variant(faculty175_variant_t variant, faculty175_face_profile_t *out)
{
    if (out == NULL) {
        return false;
    }
    switch (variant) {
        case FACULTY175_VARIANT_POCKET:
            *out = FACULTY175_FACE_PROFILE_DEFAULT;
            return true;
        case FACULTY175_VARIANT_ELECROW_128:
            *out = FACULTY175_FACE_PROFILE_CASTALIA;
            return true;
        case FACULTY175_VARIANT_ASTROLABE:
            *out = FACULTY175_FACE_PROFILE_CASTALIA;
            return true;
        case FACULTY175_VARIANT_LUNASAY:
            *out = FACULTY175_FACE_PROFILE_LUNASAY;
            return true;
        case FACULTY175_VARIANT_OCARINA:
            *out = FACULTY175_FACE_PROFILE_OCARINA;
            return true;
        case FACULTY175_VARIANT_CAMEO:
            *out = FACULTY175_FACE_PROFILE_CAMEO;
            return true;
        case FACULTY175_VARIANT_CYBER:
            *out = FACULTY175_FACE_PROFILE_CYBER;
            return true;
        default:
            *out = FACULTY175_FACE_PROFILE_DEFAULT;
            return true;
    }
}

bool faculty175_variant_from_profile(faculty175_face_profile_t profile, faculty175_variant_t *out)
{
    if (out == NULL) {
        return false;
    }
    switch (profile) {
        case FACULTY175_FACE_PROFILE_DEFAULT:
            *out = FACULTY175_VARIANT_POCKET;
            return true;
        case FACULTY175_FACE_PROFILE_CASTALIA:
            *out = FACULTY175_VARIANT_ASTROLABE;
            return true;
        case FACULTY175_FACE_PROFILE_LUNASAY:
            *out = FACULTY175_VARIANT_LUNASAY;
            return true;
        case FACULTY175_FACE_PROFILE_OCARINA:
            *out = FACULTY175_VARIANT_OCARINA;
            return true;
        case FACULTY175_FACE_PROFILE_CAMEO:
            *out = FACULTY175_VARIANT_CAMEO;
            return true;
        case FACULTY175_FACE_PROFILE_CYBER:
            *out = FACULTY175_VARIANT_CYBER;
            return true;
        default:
            return false;
    }
}

const char *faculty175_variant_label(faculty175_variant_t variant)
{
    switch (variant) {
        case FACULTY175_VARIANT_ASTROLABE:
            return "Astrolabe";
        case FACULTY175_VARIANT_LUNASAY:
            return "Lunasay";
        case FACULTY175_VARIANT_OCARINA:
            return "Ocarina";
        case FACULTY175_VARIANT_CAMEO:
            return "Cameo";
        case FACULTY175_VARIANT_LUOPAN:
            return "Luopan";
        case FACULTY175_VARIANT_ENSO:
            return "Enso";
        case FACULTY175_VARIANT_SMART_SPEAKER:
            return "SmartSpeaker";
        case FACULTY175_VARIANT_BABEL_FISH:
            return "BabelFish";
        case FACULTY175_VARIANT_ELECROW_128:
            return "Elecrow128";
        case FACULTY175_VARIANT_CYBER:
            return "Cyber";
        default:
            return "Pocket";
    }
}

const char *faculty175_variant_ota_channel(faculty175_variant_t variant)
{
    switch (variant) {
        case FACULTY175_VARIANT_ASTROLABE:
            return "astrolabe-astrolabe-175";
        case FACULTY175_VARIANT_LUNASAY:
            return "astrolabe-lunasay-175";
        case FACULTY175_VARIANT_OCARINA:
            return "astrolabe-ocarina-175";
        case FACULTY175_VARIANT_CAMEO:
            return "astrolabe-cameo-175";
        case FACULTY175_VARIANT_LUOPAN:
            return "astrolabe-luopan-175";
        case FACULTY175_VARIANT_ENSO:
            return "astrolabe-enso-175";
        case FACULTY175_VARIANT_SMART_SPEAKER:
            return "astrolabe-smart-speaker-175";
        case FACULTY175_VARIANT_BABEL_FISH:
            return "astrolabe-babel-fish-175";
        case FACULTY175_VARIANT_ELECROW_128:
            return "astrolabe-elecrow-128-175";
        case FACULTY175_VARIANT_CYBER:
            return "astrolabe-cyber-175";
        default:
            return "dev";
    }
}

bool faculty175_variant_profile_from_slug(const char *slug, faculty175_face_profile_t *out)
{
    if (slug == NULL || out == NULL) {
        return false;
    }
    if (strcasecmp(slug, "Pocket") == 0 || strcasecmp(slug, "default") == 0) {
        *out = FACULTY175_FACE_PROFILE_DEFAULT;
        return true;
    }
    if (strcasecmp(slug, "Astrolabe") == 0 || strcasecmp(slug, "Castalia") == 0) {
        *out = FACULTY175_FACE_PROFILE_CASTALIA;
        return true;
    }
    if (strcasecmp(slug, "Lunasay") == 0 || strcasecmp(slug, "LunaSay") == 0) {
        *out = FACULTY175_FACE_PROFILE_LUNASAY;
        return true;
    }
    if (strcasecmp(slug, "Ocarina") == 0) {
        *out = FACULTY175_FACE_PROFILE_OCARINA;
        return true;
    }
    if (strcasecmp(slug, "Cameo") == 0) {
        *out = FACULTY175_FACE_PROFILE_CAMEO;
        return true;
    }
    if (strcasecmp(slug, "Cyber") == 0) {
        *out = FACULTY175_FACE_PROFILE_CYBER;
        return true;
    }
    if (strcasecmp(slug, "Elecrow128") == 0 || strcasecmp(slug, "Elecrow") == 0) {
        *out = FACULTY175_FACE_PROFILE_CASTALIA;
        return true;
    }
    return false;
}

bool faculty175_variant_profile_from_nvs(faculty175_face_profile_t *out, bool *found_out)
{
    if (out == NULL || found_out == NULL) {
        return false;
    }
    *out = default_profile();
    *found_out = false;

    if (forced_profile(out)) {
        *found_out = true;
        return true;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(VARIANT_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND;
    }

    uint8_t value = 0;
    err = nvs_get_u8(nvs, VARIANT_NVS_KEY, &value);
    if (err == ESP_OK && variant_valid(value)) {
        const bool ok = profile_from_variant((faculty175_variant_t)value, out);
        *found_out = ok;
        nvs_close(nvs);
        return ok;
    }

    char label[24] = {};
    size_t label_len = sizeof(label);
    err = nvs_get_str(nvs, VARIANT_NVS_FW_KEY, label, &label_len);
    nvs_close(nvs);
    if (err == ESP_OK && faculty175_variant_profile_from_slug(label, out)) {
        *found_out = true;
        return true;
    }
    return err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_TYPE_MISMATCH || err == ESP_OK;
}

esp_err_t faculty175_variant_persist_for_profile(faculty175_face_profile_t profile)
{
    faculty175_variant_t variant;
    if (!faculty175_variant_from_profile(profile, &variant)) {
        return ESP_OK;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(VARIANT_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, VARIANT_NVS_KEY, (uint8_t)variant);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, VARIANT_NVS_FW_KEY, faculty175_variant_label(variant));
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, VARIANT_NVS_PLATFORM_KEY, ASTROLABE_DEVICE_PLATFORM);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, VARIANT_NVS_OTA_KEY, faculty175_variant_ota_channel(variant));
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        FACULTY175_LOG_STAGE(TAG,
                             "variant",
                             "profile=%s variant=%s channel=%s",
                             faculty175_face_profile_slug(profile),
                             faculty175_variant_label(variant),
                             faculty175_variant_ota_channel(variant));
    }
    return err;
}
