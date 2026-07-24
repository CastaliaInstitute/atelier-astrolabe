#include "faculty175_face_profile.h"

#include <string.h>
#include <strings.h>

#include "nvs.h"

#include "faculty175_log.h"

static const char *TAG = "faculty175_face_profile";

#define FACES_NVS_NS "faces"
#define FACES_NVS_PROFILE "profile"

#ifndef ASTROLABE185B_CLAW_VARIANT
#define ASTROLABE185B_CLAW_VARIANT 0
#endif

static faculty175_face_profile_t s_profile = FACULTY175_FACE_PROFILE_DEFAULT;

static bool face_is_anchor(faculty175_face_id_t id)
{
#if ASTROLABE185B_CLAW_VARIANT
    if (id == FACULTY175_FACE_ALPHEUS) {
        return true;
    }
#endif
    return id == FACULTY175_FACE_POCKETWATCH || id == FACULTY175_FACE_SETTINGS;
}

static bool face_is_nav_anchor(faculty175_face_id_t id)
{
#if ASTROLABE185B_CLAW_VARIANT
    if (id == FACULTY175_FACE_ALPHEUS) {
        return true;
    }
#endif
    return id == FACULTY175_FACE_POCKETWATCH;
}

static bool face_default_navigation_enabled(faculty175_face_id_t id)
{
    const faculty175_face_desc_t *face = faculty175_faces_get(id);
    if (face == NULL || id == FACULTY175_FACE_SETTINGS) {
        return false;
    }
    return face->enabled_by_default;
}

static bool face_in_list(faculty175_face_id_t id, const faculty175_face_id_t *list, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (list[i] == id) {
            return true;
        }
    }
    return false;
}

static const faculty175_face_id_t k_secops_faces[] = {
    FACULTY175_FACE_WSCAN,
    FACULTY175_FACE_DEAUTH,
    FACULTY175_FACE_EVILTWIN,
    FACULTY175_FACE_HANDSHAKE,
    FACULTY175_FACE_WATCHER,
    FACULTY175_FACE_INCIDENTS,
};

static const faculty175_face_id_t k_fortune_faces[] = {
    FACULTY175_FACE_TAROT,
    FACULTY175_FACE_LENORMAND,
    FACULTY175_FACE_RUNES,
    FACULTY175_FACE_GEOMANCY,
    FACULTY175_FACE_PYTHIA,
    FACULTY175_FACE_ENOCHIAN,
    FACULTY175_FACE_ALETHIOMETER,
    FACULTY175_FACE_INQ,
};

static const faculty175_face_id_t k_lunasay_faces[] = {
    FACULTY175_FACE_MOON,
    FACULTY175_FACE_ASTROLOGY,
    FACULTY175_FACE_SYNASTRY,
    FACULTY175_FACE_TRANSITS,
    FACULTY175_FACE_SKY,
    FACULTY175_FACE_ALMANAC,
    FACULTY175_FACE_PHENOLOGY,
    FACULTY175_FACE_SOLAR,
    FACULTY175_FACE_MAGNETOSPHERE,
};

static const faculty175_face_id_t k_castalia_faces[] = {
    FACULTY175_FACE_CLASSIC,
    FACULTY175_FACE_APOCALYPSO,
    FACULTY175_FACE_DIGITAL,
    FACULTY175_FACE_CALCIFER,
    FACULTY175_FACE_CASTALIA,
    FACULTY175_FACE_WEATHER,
    FACULTY175_FACE_GLOBE,
    FACULTY175_FACE_RADAR,
    FACULTY175_FACE_ROCKET,
    FACULTY175_FACE_FOCUS,
    FACULTY175_FACE_BIOMETRICS,
    FACULTY175_FACE_WATCHER,
    FACULTY175_FACE_LEVEL,
    FACULTY175_FACE_MAZE,
    FACULTY175_FACE_DEATHSTAR,
    FACULTY175_FACE_TRON,
    FACULTY175_FACE_SCALE,
    FACULTY175_FACE_HID,
    FACULTY175_FACE_LINUX,
    FACULTY175_FACE_SPOTIFY,
};

static const faculty175_face_id_t k_ocarina_faces[] = {
    FACULTY175_FACE_OCARINA,
    FACULTY175_FACE_PITCH,
    FACULTY175_FACE_BONGO,
    FACULTY175_FACE_PIANO,
    FACULTY175_FACE_KALIMBA,
    FACULTY175_FACE_DRONE,
    FACULTY175_FACE_CHORD,
    FACULTY175_FACE_LEVEL,
    FACULTY175_FACE_TUNING,
    FACULTY175_FACE_SPECTRUM,
    FACULTY175_FACE_CHAKRA,
    FACULTY175_FACE_BOWL,
    FACULTY175_FACE_PANDRUM,
    FACULTY175_FACE_ORIENT,
};

static const faculty175_face_id_t k_cameo_faces[] = {
    FACULTY175_FACE_FACULTY,
    FACULTY175_FACE_QUOTES,
    FACULTY175_FACE_NOTES,
    FACULTY175_FACE_QDAY,
    FACULTY175_FACE_BABEL,
};

static const faculty175_face_id_t *profile_faces(faculty175_face_profile_t profile, size_t *count_out)
{
    if (count_out == NULL) {
        return NULL;
    }
    switch (profile) {
        case FACULTY175_FACE_PROFILE_SECOPS:
            *count_out = sizeof(k_secops_faces) / sizeof(k_secops_faces[0]);
            return k_secops_faces;
        case FACULTY175_FACE_PROFILE_FORTUNE:
            *count_out = sizeof(k_fortune_faces) / sizeof(k_fortune_faces[0]);
            return k_fortune_faces;
        case FACULTY175_FACE_PROFILE_CASTALIA:
            *count_out = sizeof(k_castalia_faces) / sizeof(k_castalia_faces[0]);
            return k_castalia_faces;
        case FACULTY175_FACE_PROFILE_OCARINA:
            *count_out = sizeof(k_ocarina_faces) / sizeof(k_ocarina_faces[0]);
            return k_ocarina_faces;
        case FACULTY175_FACE_PROFILE_LUNASAY:
            *count_out = sizeof(k_lunasay_faces) / sizeof(k_lunasay_faces[0]);
            return k_lunasay_faces;
        case FACULTY175_FACE_PROFILE_CAMEO:
            *count_out = sizeof(k_cameo_faces) / sizeof(k_cameo_faces[0]);
            return k_cameo_faces;
        default:
            *count_out = 0;
            return NULL;
    }
}

static esp_err_t persist_profile(faculty175_face_profile_t profile)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, FACES_NVS_PROFILE, (uint8_t)profile);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t restore_default_faces(void)
{
    esp_err_t err = ESP_OK;
    for (faculty175_face_id_t id = 0; id < FACULTY175_FACE_COUNT; ++id) {
        const faculty175_face_desc_t *face = faculty175_faces_get(id);
        if (face == NULL || !face->ported) {
            continue;
        }
        const bool enabled = face->enabled_by_default || face_is_anchor(id);
        const bool nav = face_is_nav_anchor(id) || face_default_navigation_enabled(id);
        esp_err_t step = faculty175_faces_set_enabled(id, enabled);
        if (step != ESP_OK && err == ESP_OK) {
            err = step;
        }
        step = faculty175_faces_set_navigation_enabled(id, nav);
        if (step != ESP_OK && err == ESP_OK) {
            err = step;
        }
    }
    return err;
}

static esp_err_t apply_profile_faces(faculty175_face_profile_t profile)
{
    size_t count = 0;
    const faculty175_face_id_t *allowed = profile_faces(profile, &count);
    esp_err_t err = ESP_OK;

    for (faculty175_face_id_t id = 0; id < FACULTY175_FACE_COUNT; ++id) {
        const faculty175_face_desc_t *face = faculty175_faces_get(id);
        if (face == NULL || !face->ported) {
            continue;
        }

        bool enable = false;
        bool nav = false;
        if (face_is_anchor(id)) {
            enable = true;
            nav = face_is_nav_anchor(id);
        } else if (allowed != NULL && face_in_list(id, allowed, count)) {
            enable = true;
            nav = true;
        }

        esp_err_t step = faculty175_faces_set_enabled(id, enable);
        if (step != ESP_OK && err == ESP_OK) {
            err = step;
        }
        step = faculty175_faces_set_navigation_enabled(id, nav);
        if (step != ESP_OK && err == ESP_OK) {
            err = step;
        }
    }
    return err;
}

bool faculty175_face_profile_face_allowed(faculty175_face_profile_t profile, faculty175_face_id_t id)
{
    if (face_is_anchor(id)) {
        return true;
    }
    if (profile == FACULTY175_FACE_PROFILE_DEFAULT) {
        const faculty175_face_desc_t *face = faculty175_faces_get(id);
        return face != NULL && face->enabled_by_default;
    }
    size_t count = 0;
    const faculty175_face_id_t *allowed = profile_faces(profile, &count);
    return allowed != NULL && face_in_list(id, allowed, count);
}

faculty175_face_id_t faculty175_face_profile_home_face(faculty175_face_profile_t profile)
{
    switch (profile) {
        case FACULTY175_FACE_PROFILE_SECOPS:
            return FACULTY175_FACE_WSCAN;
        case FACULTY175_FACE_PROFILE_FORTUNE:
            return FACULTY175_FACE_TAROT;
        case FACULTY175_FACE_PROFILE_CASTALIA:
            return FACULTY175_FACE_CLASSIC;
        case FACULTY175_FACE_PROFILE_OCARINA:
            return FACULTY175_FACE_OCARINA;
        case FACULTY175_FACE_PROFILE_LUNASAY:
            return FACULTY175_FACE_MOON;
        case FACULTY175_FACE_PROFILE_CAMEO:
            return FACULTY175_FACE_FACULTY;
        default:
#if ASTROLABE185B_CLAW_VARIANT
            return FACULTY175_FACE_ALPHEUS;
#else
            return FACULTY175_FACE_POCKETWATCH;
#endif
    }
}

esp_err_t faculty175_face_profile_apply(faculty175_face_profile_t profile, bool persist)
{
    if (profile > FACULTY175_FACE_PROFILE_CAMEO) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = profile == FACULTY175_FACE_PROFILE_DEFAULT ? restore_default_faces()
                                                               : apply_profile_faces(profile);
    if (err != ESP_OK) {
        return err;
    }

    s_profile = profile;
    const faculty175_face_id_t home = faculty175_face_profile_home_face(profile);
    const faculty175_face_desc_t *current = faculty175_faces_current();
    if (current != NULL && !faculty175_faces_navigation_enabled(current->id)) {
        (void)faculty175_faces_set_runtime(home);
    }

    FACULTY175_LOG_STAGE(TAG,
                         "profile",
                         "apply %s (%s)",
                         faculty175_face_profile_slug(profile),
                         faculty175_face_profile_label(profile));
    if (persist) {
        err = persist_profile(profile);
    }
    return err;
}

esp_err_t faculty175_face_profile_init(void)
{
    uint8_t stored = (uint8_t)FACULTY175_FACE_PROFILE_DEFAULT;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        const esp_err_t get_err = nvs_get_u8(nvs, FACES_NVS_PROFILE, &stored);
        nvs_close(nvs);
        if (get_err != ESP_OK && get_err != ESP_ERR_NVS_NOT_FOUND) {
            return get_err;
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    if (stored > (uint8_t)FACULTY175_FACE_PROFILE_CAMEO) {
        stored = (uint8_t)FACULTY175_FACE_PROFILE_DEFAULT;
    }
    s_profile = (faculty175_face_profile_t)stored;
    return faculty175_face_profile_apply(s_profile, false);
}

faculty175_face_profile_t faculty175_face_profile_current(void)
{
    return s_profile;
}

const char *faculty175_face_profile_slug(faculty175_face_profile_t profile)
{
    switch (profile) {
        case FACULTY175_FACE_PROFILE_SECOPS:
            return "secops";
        case FACULTY175_FACE_PROFILE_FORTUNE:
            return "fortune";
        case FACULTY175_FACE_PROFILE_CASTALIA:
            return "castalia";
        case FACULTY175_FACE_PROFILE_OCARINA:
            return "ocarina";
        case FACULTY175_FACE_PROFILE_LUNASAY:
            return "lunasay";
        case FACULTY175_FACE_PROFILE_CAMEO:
            return "cameo";
        default:
            return "default";
    }
}

const char *faculty175_face_profile_label(faculty175_face_profile_t profile)
{
    switch (profile) {
        case FACULTY175_FACE_PROFILE_SECOPS:
            return "SecOps";
        case FACULTY175_FACE_PROFILE_FORTUNE:
            return "Fortune Telling";
        case FACULTY175_FACE_PROFILE_CASTALIA:
            return "Castalia";
        case FACULTY175_FACE_PROFILE_OCARINA:
            return "Ocarina";
        case FACULTY175_FACE_PROFILE_LUNASAY:
            return "LunaSay";
        case FACULTY175_FACE_PROFILE_CAMEO:
            return "Cameo";
        default:
            return "Default";
    }
}

bool faculty175_face_profile_from_slug(const char *slug, faculty175_face_profile_t *out)
{
    if (slug == NULL || out == NULL) {
        return false;
    }
    if (strcasecmp(slug, "secops") == 0) {
        *out = FACULTY175_FACE_PROFILE_SECOPS;
        return true;
    }
    if (strcasecmp(slug, "fortune") == 0 || strcasecmp(slug, "fortune-telling") == 0) {
        *out = FACULTY175_FACE_PROFILE_FORTUNE;
        return true;
    }
    if (strcasecmp(slug, "castalia") == 0) {
        *out = FACULTY175_FACE_PROFILE_CASTALIA;
        return true;
    }
    if (strcasecmp(slug, "ocarina") == 0) {
        *out = FACULTY175_FACE_PROFILE_OCARINA;
        return true;
    }
    if (strcasecmp(slug, "lunasay") == 0) {
        *out = FACULTY175_FACE_PROFILE_LUNASAY;
        return true;
    }
    if (strcasecmp(slug, "cameo") == 0 || strcasecmp(slug, "camea") == 0) {
        *out = FACULTY175_FACE_PROFILE_CAMEO;
        return true;
    }
    if (strcasecmp(slug, "default") == 0) {
        *out = FACULTY175_FACE_PROFILE_DEFAULT;
        return true;
    }
    return false;
}
