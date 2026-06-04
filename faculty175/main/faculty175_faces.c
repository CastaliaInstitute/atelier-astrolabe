#include "faculty175_faces.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "faculty175_log.h"

static const char *TAG = "faculty175_faces";

#define FACES_NVS_NS "faces"
#define FACES_NVS_CURRENT "current"
#define FACES_NVS_SCHEMA "schema"
#define FACE_KEY_CAP 16
#define FACES_SCHEMA_VERSION 9

static const faculty175_face_desc_t k_faces[] = {
    { FACULTY175_FACE_FACULTY, "faculty", "Faculty", FACULTY175_FACE_CAT_HOME, true, true, 0 },
    { FACULTY175_FACE_CLASSIC, "classic", "Classic Analog", FACULTY175_FACE_CAT_HOME, true, true, 5 },
    { FACULTY175_FACE_APOCALYPSO, "apocalypso", "Apocalypso", FACULTY175_FACE_CAT_HOME, true, true, 6 },
    { FACULTY175_FACE_DIGITAL, "digital", "Digital Local", FACULTY175_FACE_CAT_HOME, true, true, 7 },
    { FACULTY175_FACE_SPOTIFY, "spotify", "Spotify", FACULTY175_FACE_CAT_HOME, true, true, 8 },
    { FACULTY175_FACE_NOTES, "notes", "Notes", FACULTY175_FACE_CAT_COMMONPLACE, true, true, 10 },
    { FACULTY175_FACE_MOON, "moon", "Moon", FACULTY175_FACE_CAT_ORACLE, true, true, 20 },
    { FACULTY175_FACE_CALCIFER, "calcifer", "Calcifer", FACULTY175_FACE_CAT_HOME, true, true, 25 },
    { FACULTY175_FACE_CASTALIA, "castalia", "Castalia", FACULTY175_FACE_CAT_SYSTEM, true, true, 26 },
    { FACULTY175_FACE_ASTROLOGY, "astrology", "Astrology", FACULTY175_FACE_CAT_ORACLE, true, true, 30 },
    { FACULTY175_FACE_SYNASTRY, "synastry", "Synastry", FACULTY175_FACE_CAT_ORACLE, true, true, 35 },
    { FACULTY175_FACE_TAROT, "tarot", "Tarot", FACULTY175_FACE_CAT_ORACLE, true, true, 40 },
    { FACULTY175_FACE_INQ, "inq", "iNQ Card", FACULTY175_FACE_CAT_ORACLE, true, true, 45 },
    { FACULTY175_FACE_RUNES, "runes", "Runes", FACULTY175_FACE_CAT_ORACLE, true, true, 50 },
    { FACULTY175_FACE_ALETHIOMETER, "alethiometer", "Alethiometer", FACULTY175_FACE_CAT_ORACLE, true, true, 55 },
    { FACULTY175_FACE_SPECTRUM, "spectrum", "Spectrum", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 60 },
    { FACULTY175_FACE_CHAKRA, "chakra", "Chakra", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 65 },
    { FACULTY175_FACE_BOWL, "bowl", "Tibetan Bowl", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 66 },
    { FACULTY175_FACE_ROCKET, "rocket", "Rocket", FACULTY175_FACE_CAT_HOME, true, true, 70 },
    { FACULTY175_FACE_RADAR, "radar", "Radar", FACULTY175_FACE_CAT_HOME, true, true, 75 },
    { FACULTY175_FACE_WEATHER, "weather", "Weather", FACULTY175_FACE_CAT_HOME, true, true, 80 },
    { FACULTY175_FACE_GLOBE, "globe", "Globe", FACULTY175_FACE_CAT_HOME, true, true, 85 },
    { FACULTY175_FACE_SCALE, "scale", "Scale Atlas", FACULTY175_FACE_CAT_HOME, true, true, 1 },
    { FACULTY175_FACE_ALMANAC, "almanac", "Almanac", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_ORACLE, true, true, 2 },
    { FACULTY175_FACE_SKY, "sky", "Sky", FACULTY175_FACE_CAT_HOME, true, true, 90 },
    { FACULTY175_FACE_QUOTES, "quotes", "Quotes", FACULTY175_FACE_CAT_COMMONPLACE, true, true, 95 },
    { FACULTY175_FACE_TRANSITS, "transits", "Live Transits", FACULTY175_FACE_CAT_ORACLE, true, true, 100 },
    { FACULTY175_FACE_OCARINA, "ocarina", "Ocarina", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 110 },
    { FACULTY175_FACE_PITCH, "pitch", "Pitch Pipe", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 111 },
    { FACULTY175_FACE_BONGO, "bongo", "Bongo", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 112 },
    { FACULTY175_FACE_PIANO, "piano", "Piano", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 113 },
    { FACULTY175_FACE_KALIMBA, "kalimba", "Kalimba", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 114 },
    { FACULTY175_FACE_DRONE, "drone", "Drone", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 115 },
    { FACULTY175_FACE_CHORD, "chord", "Chord", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 116 },
    { FACULTY175_FACE_LEVEL, "level", "Level", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 120 },
    { FACULTY175_FACE_TUNING, "tuning", "Tuning", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 121 },
    { FACULTY175_FACE_PANDRUM, "pandrum", "Pan Drum", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 122 },
    { FACULTY175_FACE_ORIENT, "orient", "Orientation", FACULTY175_FACE_CAT_INSTRUMENT, true, true, 130 },
    { FACULTY175_FACE_LUOPAN, "luopan", "Luopan", FACULTY175_FACE_CAT_ORACLE, true, true, 131 },
    { FACULTY175_FACE_QDAY, "qday", "Question Day", FACULTY175_FACE_CAT_COMMONPLACE, true, true, 140 },
    { FACULTY175_FACE_FOCUS, "focus", "Focus Timer", FACULTY175_FACE_CAT_HOME, true, true, 145 },
    { FACULTY175_FACE_BIOMETRICS, "bio", "Biometrics", FACULTY175_FACE_CAT_HOME, true, true, 150 },
    { FACULTY175_FACE_WATCHER, "watcher", "Watcher", FACULTY175_FACE_CAT_HOME, true, true, 155 },
    { FACULTY175_FACE_LENORMAND, "lenormand", "Lenormand", FACULTY175_FACE_CAT_ORACLE, true, true, 160 },
    { FACULTY175_FACE_PYTHIA, "pythia", "Pythia", FACULTY175_FACE_CAT_ORACLE, true, true, 165 },
    { FACULTY175_FACE_GEOMANCY, "geomancy", "Geomancy", FACULTY175_FACE_CAT_ORACLE, true, true, 170 },
    { FACULTY175_FACE_ENOCHIAN, "enochian", "Enochian Angel", FACULTY175_FACE_CAT_ORACLE, true, true, 175 },
    { FACULTY175_FACE_HID, "hid", "HID Touchpad", FACULTY175_FACE_CAT_SYSTEM, true, true, 200 },
    { FACULTY175_FACE_BABEL, "babel", "Babel Fish", FACULTY175_FACE_CAT_COMMONPLACE, true, true, 205 },
    { FACULTY175_FACE_DEATHSTAR, "deathstar", "Death Star", FACULTY175_FACE_CAT_HOME, true, true, 207 },
    { FACULTY175_FACE_SETTINGS, "settings", "Settings", FACULTY175_FACE_CAT_SYSTEM, true, true, 250 },
};

static faculty175_face_id_t s_current = FACULTY175_FACE_FACULTY;

static void enabled_key(const faculty175_face_desc_t *face, char *out, size_t cap)
{
    snprintf(out, cap, "en_%s", face != NULL ? face->slug : "");
}

static void order_key(const faculty175_face_desc_t *face, char *out, size_t cap)
{
    snprintf(out, cap, "ord_%s", face != NULL ? face->slug : "");
}

static esp_err_t nvs_get_u8_or_default(const char *key, uint8_t fallback, uint8_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = fallback;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u8(nvs, key, out);
    nvs_close(nvs);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static esp_err_t nvs_set_u8_value(const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static bool face_valid(faculty175_face_id_t id)
{
    return id >= 0 && id < FACULTY175_FACE_COUNT;
}

static int ordered_compare(const faculty175_face_desc_t *a, const faculty175_face_desc_t *b)
{
    const uint8_t ao = faculty175_faces_order(a->id);
    const uint8_t bo = faculty175_faces_order(b->id);
    if (ao != bo) {
        return (int)ao - (int)bo;
    }
    return (int)a->id - (int)b->id;
}

static const faculty175_face_desc_t *next_enabled_from(faculty175_face_id_t from, int delta)
{
    const faculty175_face_desc_t *best = NULL;
    for (size_t pass = 0; pass < 2 && best == NULL; ++pass) {
        for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
            const faculty175_face_desc_t *candidate = &k_faces[i];
            if (!faculty175_faces_enabled(candidate->id)) {
                continue;
            }
            if (delta >= 0) {
                if (pass == 0 && ordered_compare(candidate, &k_faces[from]) <= 0) {
                    continue;
                }
                if (best == NULL || ordered_compare(candidate, best) < 0) {
                    best = candidate;
                }
            } else {
                if (pass == 0 && ordered_compare(candidate, &k_faces[from]) >= 0) {
                    continue;
                }
                if (best == NULL || ordered_compare(candidate, best) > 0) {
                    best = candidate;
                }
            }
        }
    }
    return best;
}

esp_err_t faculty175_faces_init(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    char current[24] = {};
    size_t len = sizeof(current);
    err = nvs_get_str(nvs, FACES_NVS_CURRENT, current, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_str(nvs, FACES_NVS_CURRENT, k_faces[FACULTY175_FACE_FACULTY].slug);
        current[0] = '\0';
    }
    uint8_t schema = 0;
    if (err == ESP_OK && nvs_get_u8(nvs, FACES_NVS_SCHEMA, &schema) == ESP_ERR_NVS_NOT_FOUND) {
        schema = 0;
    }

    for (size_t i = 0; i < FACULTY175_FACE_COUNT && err == ESP_OK; ++i) {
        char key[FACE_KEY_CAP];
        uint8_t value = 0;
        enabled_key(&k_faces[i], key, sizeof(key));
        if (nvs_get_u8(nvs, key, &value) == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_set_u8(nvs, key, k_faces[i].enabled_by_default ? 1 : 0);
        }
        order_key(&k_faces[i], key, sizeof(key));
        if (err == ESP_OK && nvs_get_u8(nvs, key, &value) == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_set_u8(nvs, key, k_faces[i].default_order);
        }
    }
    if (err == ESP_OK && schema < FACES_SCHEMA_VERSION) {
        for (size_t i = 0; i < FACULTY175_FACE_COUNT && err == ESP_OK; ++i) {
            if (!k_faces[i].ported || !k_faces[i].enabled_by_default) {
                continue;
            }
            char key[FACE_KEY_CAP];
            enabled_key(&k_faces[i], key, sizeof(key));
            err = nvs_set_u8(nvs, key, 1);
            if (err == ESP_OK) {
                order_key(&k_faces[i], key, sizeof(key));
                err = nvs_set_u8(nvs, key, k_faces[i].default_order);
            }
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(nvs, FACES_NVS_SCHEMA, FACES_SCHEMA_VERSION);
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    const faculty175_face_desc_t *saved = faculty175_faces_find(current);
    if (saved != NULL && faculty175_faces_enabled(saved->id)) {
        s_current = saved->id;
    } else {
        s_current = FACULTY175_FACE_FACULTY;
    }
    FACULTY175_LOG_STAGE(TAG, "faces", "current=%s", faculty175_faces_current()->slug);
    return ESP_OK;
}

const faculty175_face_desc_t *faculty175_faces_current(void)
{
    return &k_faces[s_current];
}

const faculty175_face_desc_t *faculty175_faces_find(const char *slug)
{
    if (slug == NULL || slug[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        if (strcasecmp(slug, k_faces[i].slug) == 0) {
            return &k_faces[i];
        }
    }
    return NULL;
}

const faculty175_face_desc_t *faculty175_faces_get(faculty175_face_id_t id)
{
    return face_valid(id) ? &k_faces[id] : NULL;
}

esp_err_t faculty175_faces_set_runtime(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!faculty175_faces_enabled(id)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_current = id;
    FACULTY175_LOG_STAGE(TAG, "faces", "set %s%s", k_faces[id].slug, k_faces[id].ported ? "" : " (not ported)");
    return ESP_OK;
}

esp_err_t faculty175_faces_save_current(void)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t nvs;
    if (nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        err = nvs_set_str(nvs, FACES_NVS_CURRENT, k_faces[s_current].slug);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    return err;
}

esp_err_t faculty175_faces_set(faculty175_face_id_t id)
{
    const esp_err_t err = faculty175_faces_set_runtime(id);
    return err == ESP_OK ? faculty175_faces_save_current() : err;
}

esp_err_t faculty175_faces_set_enabled(faculty175_face_id_t id, bool enabled)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (id == FACULTY175_FACE_FACULTY && !enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[FACE_KEY_CAP];
    enabled_key(&k_faces[id], key, sizeof(key));
    const esp_err_t err = nvs_set_u8_value(key, enabled ? 1 : 0);
    if (err == ESP_OK && !enabled && s_current == id) {
        const faculty175_face_desc_t *next = next_enabled_from(id, 1);
        s_current = next != NULL ? next->id : FACULTY175_FACE_FACULTY;
    }
    return err;
}

esp_err_t faculty175_faces_set_order(faculty175_face_id_t id, uint8_t order)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[FACE_KEY_CAP];
    order_key(&k_faces[id], key, sizeof(key));
    return nvs_set_u8_value(key, order);
}

bool faculty175_faces_enabled(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return false;
    }
    char key[FACE_KEY_CAP];
    enabled_key(&k_faces[id], key, sizeof(key));
    uint8_t enabled = k_faces[id].enabled_by_default ? 1 : 0;
    (void)nvs_get_u8_or_default(key, enabled, &enabled);
    return enabled != 0;
}

uint8_t faculty175_faces_order(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return 255;
    }
    char key[FACE_KEY_CAP];
    order_key(&k_faces[id], key, sizeof(key));
    uint8_t order = k_faces[id].default_order;
    (void)nvs_get_u8_or_default(key, order, &order);
    return order;
}

const faculty175_face_desc_t *faculty175_faces_cycle(int delta)
{
    const faculty175_face_desc_t *next = next_enabled_from(s_current, delta);
    if (next != NULL) {
        (void)faculty175_faces_set(next->id);
    }
    return faculty175_faces_current();
}

const faculty175_face_desc_t *faculty175_faces_cycle_runtime(int delta)
{
    const faculty175_face_desc_t *next = next_enabled_from(s_current, delta);
    if (next != NULL) {
        (void)faculty175_faces_set_runtime(next->id);
    }
    return faculty175_faces_current();
}

const faculty175_face_desc_t *faculty175_faces_nav_at(size_t index)
{
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        const faculty175_face_desc_t *candidate = &k_faces[i];
        if (!faculty175_faces_enabled(candidate->id)) {
            continue;
        }
        size_t before = 0;
        for (size_t j = 0; j < FACULTY175_FACE_COUNT; ++j) {
            const faculty175_face_desc_t *other = &k_faces[j];
            if (faculty175_faces_enabled(other->id) && ordered_compare(other, candidate) < 0) {
                ++before;
            }
        }
        if (before == index) {
            return candidate;
        }
    }
    return NULL;
}

size_t faculty175_faces_count(void)
{
    return FACULTY175_FACE_COUNT;
}

size_t faculty175_faces_enabled_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        if (faculty175_faces_enabled(k_faces[i].id)) {
            ++count;
        }
    }
    return count;
}

bool faculty175_faces_nav_position(size_t *out_index, size_t *out_count)
{
    if (out_index == NULL || out_count == NULL || !face_valid(s_current)) {
        return false;
    }

    size_t index = 0;
    size_t count = 0;
    const faculty175_face_desc_t *current = &k_faces[s_current];
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        const faculty175_face_desc_t *candidate = &k_faces[i];
        if (!faculty175_faces_enabled(candidate->id)) {
            continue;
        }
        if (ordered_compare(candidate, current) < 0) {
            ++index;
        }
        ++count;
    }

    *out_index = index < count ? index : 0;
    *out_count = count;
    return count > 0;
}

static void print_face_line(const faculty175_face_desc_t *face)
{
    printf("face: %-10s enabled=%s order=%u ported=%s categories=0x%02lx%s\n",
           face->slug,
           faculty175_faces_enabled(face->id) ? "yes" : "no",
           (unsigned)faculty175_faces_order(face->id),
           face->ported ? "yes" : "no",
           (unsigned long)face->categories,
           face->id == s_current ? " current" : "");
}

bool faculty175_faces_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "faces") != 0 && strncasecmp(line, "faces ", 6) != 0 &&
                         strcasecmp(line, "face") != 0 && strncasecmp(line, "face ", 5) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "";
    while (*sub == ' ') {
        ++sub;
    }

    if (*sub == '\0' || strcasecmp(sub, "status") == 0 || strcasecmp(sub, "list") == 0) {
        printf("faces: current=%s count=%u\n", faculty175_faces_current()->slug, (unsigned)FACULTY175_FACE_COUNT);
        for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
            print_face_line(&k_faces[i]);
        }
        fflush(stdout);
        return true;
    }

    char cmd[16] = {};
    char slug[24] = {};
    unsigned value = 0;
    if (sscanf(sub, "%15s %23s %u", cmd, slug, &value) < 1) {
        return true;
    }

    if (strcasecmp(cmd, "help") == 0) {
        printf("faces commands:\n");
        printf("  faces list\n");
        printf("  faces next\n");
        printf("  faces prev\n");
        printf("  faces set <slug>\n");
        printf("  faces enable <slug>\n");
        printf("  faces disable <slug>\n");
        printf("  faces order <slug> <0-255>\n");
        fflush(stdout);
        return true;
    }

    if (strcasecmp(cmd, "next") == 0 || strcasecmp(cmd, "prev") == 0) {
        const faculty175_face_desc_t *next = faculty175_faces_cycle(strcasecmp(cmd, "next") == 0 ? 1 : -1);
        printf("faces: current=%s\n", next != NULL ? next->slug : "-");
        fflush(stdout);
        return true;
    }

    const faculty175_face_desc_t *face = faculty175_faces_find(slug);
    if (face == NULL) {
        printf("faces: unknown face \"%s\"\n", slug);
        fflush(stdout);
        return true;
    }

    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (strcasecmp(cmd, "set") == 0) {
        err = faculty175_faces_set(face->id);
    } else if (strcasecmp(cmd, "enable") == 0) {
        err = faculty175_faces_set_enabled(face->id, true);
    } else if (strcasecmp(cmd, "disable") == 0) {
        err = faculty175_faces_set_enabled(face->id, false);
    } else if (strcasecmp(cmd, "order") == 0) {
        err = value <= 255 ? faculty175_faces_set_order(face->id, (uint8_t)value) : ESP_ERR_INVALID_ARG;
    }
    printf("faces: %s %s %s\n", cmd, face->slug, esp_err_to_name(err));
    fflush(stdout);
    return true;
}
