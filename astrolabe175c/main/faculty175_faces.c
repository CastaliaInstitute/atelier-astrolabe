#include "faculty175_faces.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "esp_log.h"
#include "nvs.h"

#include "faculty175_face_alethiometer.h"
#include "faculty175_face_runes.h"
#include "faculty175_face_profile.h"
#include "faculty175_log.h"

#ifndef ASTROLABE_CYBER_FEATURES
#define ASTROLABE_CYBER_FEATURES 0
#endif

static const char *TAG = "faculty175_faces";

#define FACES_NVS_NS "faces"
#define FACES_NVS_CURRENT "current"
#define FACES_NVS_SCHEMA "schema"
#define FACES_NVS_NAV_COUNT "navcnt"
#define FACES_NVS_NAV_MAP "navmap"
#define FACE_KEY_CAP 8
#define FACES_CONFIG_RESET_SCHEMA_VERSION 42
#define FACES_SCHEMA_VERSION 42

static const faculty175_face_desc_t k_faces[] = {
    { FACULTY175_FACE_FACULTY, "faculty", "Faculty", FACULTY175_FACE_CAT_HOME, true, true, 10 },
    { FACULTY175_FACE_CLASSIC, "classic", "Classic Analog", FACULTY175_FACE_CAT_HOME, true, false, 200 },
    { FACULTY175_FACE_APOCALYPSO, "apocalypso", "Apocalypso", FACULTY175_FACE_CAT_HOME, true, false, 210 },
    { FACULTY175_FACE_DIGITAL, "digital", "Digital Local", FACULTY175_FACE_CAT_HOME, false, false, 7 },
    { FACULTY175_FACE_SPOTIFY, "spotify", "Spotify", FACULTY175_FACE_CAT_HOME, false, false, 8 },
    { FACULTY175_FACE_NOTES, "notes", "Notes", FACULTY175_FACE_CAT_COMMONPLACE, true, true, 70 },
    { FACULTY175_FACE_MOON, "moon", "Moon", FACULTY175_FACE_CAT_ORACLE, true, false, 220 },
    { FACULTY175_FACE_CALCIFER, "calcifer", "Calcifer", FACULTY175_FACE_CAT_HOME, false, false, 25 },
    { FACULTY175_FACE_CASTALIA, "castalia", "Castalia", FACULTY175_FACE_CAT_SYSTEM, true, false, 230 },
    { FACULTY175_FACE_ASTROLOGY, "astrology", "Astrology", FACULTY175_FACE_CAT_ORACLE, true, false, 240 },
    { FACULTY175_FACE_SYNASTRY, "synastry", "Synastry", FACULTY175_FACE_CAT_ORACLE, true, false, 245 },
    { FACULTY175_FACE_PARTNER_WELLNESS, "partner-wellness", "Partner Wellness", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_ORACLE, true, true, 246 },
    { FACULTY175_FACE_TAROT, "tarot", "Tarot", FACULTY175_FACE_CAT_ORACLE, true, true, 40 },
    { FACULTY175_FACE_INQ, "inq", "iNQ Card", FACULTY175_FACE_CAT_ORACLE, true, false, 250 },
    { FACULTY175_FACE_RUNES, "runes", "Runes", FACULTY175_FACE_CAT_ORACLE, true, false, 251 },
    { FACULTY175_FACE_ALETHIOMETER, "alethiometer", "Alethiometer", FACULTY175_FACE_CAT_ORACLE, true, true, 30 },
    { FACULTY175_FACE_CRYSTAL_BALL, "crystal-ball", "Crystal Ball", FACULTY175_FACE_CAT_ORACLE, true, true, 35 },
    { FACULTY175_FACE_SPECTRUM, "spectrum", "Spectrum", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 60 },
    { FACULTY175_FACE_CHAKRA, "chakra", "Chakra", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 65 },
    { FACULTY175_FACE_BOWL, "bowl", "Tibetan Bowl", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 66 },
    { FACULTY175_FACE_ROCKET, "rocket", "Rocket", FACULTY175_FACE_CAT_HOME, true, false, 252 },
    { FACULTY175_FACE_RADAR, "radar", "Radar", FACULTY175_FACE_CAT_HOME, true, false, 253 },
    { FACULTY175_FACE_WEATHER, "weather", "Weather", FACULTY175_FACE_CAT_HOME, true, false, 254 },
    { FACULTY175_FACE_GLOBE, "globe", "Globe", FACULTY175_FACE_CAT_HOME, true, false, 255 },
    { FACULTY175_FACE_SCALE, "scale", "Scale Atlas", FACULTY175_FACE_CAT_HOME, true, false, 1 },
    { FACULTY175_FACE_ALMANAC, "almanac", "Almanac", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_ORACLE, false, false, 2 },
    { FACULTY175_FACE_PHENOLOGY, "phenology", "Phenology", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_ORACLE, false, false, 3 },
    { FACULTY175_FACE_SKY, "sky", "Sky", FACULTY175_FACE_CAT_HOME, true, false, 221 },
    { FACULTY175_FACE_QUOTES, "quotes", "Quotes", FACULTY175_FACE_CAT_COMMONPLACE, true, true, 20 },
    { FACULTY175_FACE_TRANSITS, "transits", "Live Transits", FACULTY175_FACE_CAT_ORACLE, true, false, 222 },
    { FACULTY175_FACE_OCARINA, "ocarina", "Ocarina", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 110 },
    { FACULTY175_FACE_PITCH, "pitch", "Pitch Pipe", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 111 },
    { FACULTY175_FACE_BONGO, "bongo", "Bongo", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 112 },
    { FACULTY175_FACE_PIANO, "piano", "Piano", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 113 },
    { FACULTY175_FACE_KALIMBA, "kalimba", "Kalimba", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 114 },
    { FACULTY175_FACE_DRONE, "drone", "Drone", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 115 },
    { FACULTY175_FACE_CHORD, "chord", "Chord", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 116 },
    { FACULTY175_FACE_LEVEL, "level", "Level", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 120 },
    { FACULTY175_FACE_TUNING, "tuning", "Tuning", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 121 },
    { FACULTY175_FACE_PANDRUM, "pandrum", "Pan Drum", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 122 },
    { FACULTY175_FACE_ORIENT, "orient", "Orientation", FACULTY175_FACE_CAT_INSTRUMENT, true, false, 130 },
    { FACULTY175_FACE_LUOPAN, "luopan", "Luopan", FACULTY175_FACE_CAT_ORACLE, false, false, 131 },
    { FACULTY175_FACE_QDAY, "qday", "Question Day", FACULTY175_FACE_CAT_COMMONPLACE, true, false, 140 },
    { FACULTY175_FACE_FOCUS, "focus", "Focus Timer", FACULTY175_FACE_CAT_HOME, true, false, 145 },
    { FACULTY175_FACE_BIOMETRICS, "bio", "Biometrics", FACULTY175_FACE_CAT_HOME, true, false, 150 },
    { FACULTY175_FACE_IRONMAN, "arc-reactor", "Arc Reactor", FACULTY175_FACE_CAT_HOME, true, false, 151 },
    { FACULTY175_FACE_WATCHER, "watcher", "Watcher", FACULTY175_FACE_CAT_HOME, true, false, 155 },
    { FACULTY175_FACE_LENORMAND, "lenormand", "Lenormand", FACULTY175_FACE_CAT_ORACLE, true, false, 160 },
    { FACULTY175_FACE_PYTHIA, "pythia", "Pythia", FACULTY175_FACE_CAT_ORACLE, false, false, 165 },
    { FACULTY175_FACE_GEOMANCY, "geomancy", "Geomancy", FACULTY175_FACE_CAT_ORACLE, true, false, 170 },
    { FACULTY175_FACE_ENOCHIAN, "enochian", "Enochian Angel", FACULTY175_FACE_CAT_ORACLE, true, false, 175 },
    { FACULTY175_FACE_HID, "hid", "HID Touchpad", FACULTY175_FACE_CAT_SYSTEM, ASTROLABE_CYBER_FEATURES, false, 200 },
    { FACULTY175_FACE_BABEL, "babel", "Babel Fish", FACULTY175_FACE_CAT_COMMONPLACE, true, false, 205 },
    { FACULTY175_FACE_HUMAN_DESIGN, "human-design", "Human Design", FACULTY175_FACE_CAT_ORACLE, true, false, 176 },
    { FACULTY175_FACE_MAZE, "maze", "Maze", FACULTY175_FACE_CAT_HOME, true, true, 50 },
    { FACULTY175_FACE_DEATHSTAR, "deathstar", "Death Star", FACULTY175_FACE_CAT_HOME, true, true, 60 },
    { FACULTY175_FACE_SOLAR, "solar", "Solar Activity", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_ORACLE, true, true, 18 },
    { FACULTY175_FACE_MAGNETOSPHERE, "magnetosphere", "Magnetosphere", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_ORACLE, true, false, 93 },
    { FACULTY175_FACE_TRON, "tron", "TRON", FACULTY175_FACE_CAT_HOME, false, false, 65 },
    { FACULTY175_FACE_WSCAN, "wscan", "WiFi Scan", FACULTY175_FACE_CAT_SYSTEM, true, false, 201 },
    { FACULTY175_FACE_DEAUTH, "deauth", "Deauth", FACULTY175_FACE_CAT_SYSTEM, true, false, 202 },
    { FACULTY175_FACE_EVILTWIN, "eviltwin", "Evil Twin", FACULTY175_FACE_CAT_SYSTEM, true, false, 203 },
    { FACULTY175_FACE_HANDSHAKE, "handshake", "Handshake", FACULTY175_FACE_CAT_SYSTEM, true, false, 204 },
    { FACULTY175_FACE_INCIDENTS, "incidents", "Incidents", FACULTY175_FACE_CAT_SYSTEM, true, false, 205 },
    { FACULTY175_FACE_SETTINGS, "settings", "Settings", FACULTY175_FACE_CAT_SYSTEM, true, true, 250 },
    { FACULTY175_FACE_POCKETWATCH, "pocketwatch", "Watch", FACULTY175_FACE_CAT_HOME, true, true, 0 },
    { FACULTY175_FACE_BATTERY, "battery", "Battery", FACULTY175_FACE_CAT_HOME | FACULTY175_FACE_CAT_SYSTEM, true, true, 152 },
    { FACULTY175_FACE_PSYCH_STATE, "psych-state", "Psych State", FACULTY175_FACE_CAT_COMMONPLACE, true, false, 98 },
    { FACULTY175_FACE_USB_SCREEN, "usb-screen", "USB Screen", FACULTY175_FACE_CAT_SYSTEM, ASTROLABE_CYBER_FEATURES, ASTROLABE_CYBER_FEATURES, 199 },
    { FACULTY175_FACE_JOURNAL, "journal", "Journal", FACULTY175_FACE_CAT_COMMONPLACE, true, false, 227 },
    { FACULTY175_FACE_CONVERSATION, "conversation", "Conversation", FACULTY175_FACE_CAT_COMMONPLACE, true, false, 228 },
    { FACULTY175_FACE_CODEX, "codex", "Codex Remote", FACULTY175_FACE_CAT_SYSTEM, ASTROLABE_CYBER_FEATURES, ASTROLABE_CYBER_FEATURES, 198 },
};

static faculty175_face_id_t s_current = FACULTY175_FACE_IRONMAN;
static uint8_t s_enabled_cache[FACULTY175_FACE_COUNT];
static uint8_t s_nav_cache[FACULTY175_FACE_COUNT];
static uint8_t s_order_cache[FACULTY175_FACE_COUNT];
static faculty175_face_id_t s_nav_map[FACULTY175_FACE_COUNT];
static size_t s_nav_count;
static bool s_faces_cache_loaded;

static void rebuild_navigation_map(void);
static esp_err_t persist_navigation_map_to_open_nvs(nvs_handle_t nvs);
static bool load_navigation_map_from_open_nvs(nvs_handle_t nvs);

static void enabled_key(const faculty175_face_desc_t *face, char *out, size_t cap)
{
    snprintf(out, cap, "en%02u", face != NULL ? (unsigned)face->id : 0u);
}

static void order_key(const faculty175_face_desc_t *face, char *out, size_t cap)
{
    snprintf(out, cap, "ord%02u", face != NULL ? (unsigned)face->id : 0u);
}

static void nav_key(const faculty175_face_desc_t *face, char *out, size_t cap)
{
    snprintf(out, cap, "nav%02u", face != NULL ? (unsigned)face->id : 0u);
}

static bool face_default_navigation_enabled(const faculty175_face_desc_t *face)
{
    if (face == NULL || face->id == FACULTY175_FACE_SETTINGS) {
        return false;
    }
    return face->enabled_by_default;
}

static void erase_legacy_face_key(nvs_handle_t nvs, const char *prefix, const char *slug)
{
    char key[16];
    if (slug == NULL) {
        return;
    }
    const int written = snprintf(key, sizeof(key), "%s%s", prefix, slug);
    if (written <= 0 || written >= (int)sizeof(key)) {
        return;
    }
    const esp_err_t err = nvs_erase_key(nvs, key);
    if (err == ESP_OK) {
        FACULTY175_LOG_STAGE(TAG, "faces", "erased legacy key %s", key);
    }
}

static void erase_legacy_face_keys_from_open_nvs(nvs_handle_t nvs)
{
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        erase_legacy_face_key(nvs, "en_", k_faces[i].slug);
        erase_legacy_face_key(nvs, "ord_", k_faces[i].slug);
        erase_legacy_face_key(nvs, "nav_", k_faces[i].slug);
    }
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

static void load_u8_from_open_nvs(nvs_handle_t nvs, const char *key, uint8_t fallback, uint8_t *out)
{
    uint8_t value = fallback;
    if (nvs_get_u8(nvs, key, &value) != ESP_OK) {
        value = fallback;
    }
    *out = value;
}

static void refresh_faces_cache_from_open_nvs(nvs_handle_t nvs)
{
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        char key[FACE_KEY_CAP];
        enabled_key(&k_faces[i], key, sizeof(key));
        load_u8_from_open_nvs(nvs, key, k_faces[i].enabled_by_default ? 1 : 0, &s_enabled_cache[i]);

        nav_key(&k_faces[i], key, sizeof(key));
        load_u8_from_open_nvs(nvs, key, face_default_navigation_enabled(&k_faces[i]) ? 1 : 0, &s_nav_cache[i]);

        order_key(&k_faces[i], key, sizeof(key));
        load_u8_from_open_nvs(nvs, key, k_faces[i].default_order, &s_order_cache[i]);

        if (!k_faces[i].ported) {
            s_enabled_cache[k_faces[i].id] = 0;
            s_nav_cache[k_faces[i].id] = 0;
        }
    }
    s_faces_cache_loaded = true;
}

static bool face_valid(faculty175_face_id_t id)
{
    return id >= 0 && id < FACULTY175_FACE_COUNT && id != FACULTY175_FACE_PYTHIA &&
           id != FACULTY175_FACE_LUOPAN;
}

static bool face_active_slot(size_t index)
{
    return index < FACULTY175_FACE_COUNT && k_faces[index].id != FACULTY175_FACE_PYTHIA &&
           k_faces[index].id != FACULTY175_FACE_LUOPAN;
}

static bool face_is_nav_anchor(faculty175_face_id_t id)
{
#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
    (void)id;
    return false;
#else
    return id == FACULTY175_FACE_FACULTY || id == FACULTY175_FACE_POCKETWATCH ||
           id == FACULTY175_FACE_IRONMAN;
#endif
}

static uint32_t primary_face_category(uint32_t categories)
{
    static const uint32_t order[] = {
        FACULTY175_FACE_CAT_HOME,
        FACULTY175_FACE_CAT_COMMONPLACE,
        FACULTY175_FACE_CAT_ORACLE,
        FACULTY175_FACE_CAT_INSTRUMENT,
        FACULTY175_FACE_CAT_SYSTEM,
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if ((categories & order[i]) != 0) {
            return order[i];
        }
    }
    return 0;
}

static uint32_t category_from_name(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    if (strcasecmp(name, "home") == 0) {
        return FACULTY175_FACE_CAT_HOME;
    }
    if (strcasecmp(name, "common") == 0 || strcasecmp(name, "commonplace") == 0) {
        return FACULTY175_FACE_CAT_COMMONPLACE;
    }
    if (strcasecmp(name, "oracle") == 0 || strcasecmp(name, "oracles") == 0) {
        return FACULTY175_FACE_CAT_ORACLE;
    }
    if (strcasecmp(name, "instrument") == 0 || strcasecmp(name, "instruments") == 0) {
        return FACULTY175_FACE_CAT_INSTRUMENT;
    }
    if (strcasecmp(name, "system") == 0) {
        return FACULTY175_FACE_CAT_SYSTEM;
    }
    return 0;
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

static bool face_nav_visible_cached(faculty175_face_id_t id)
{
    return face_valid(id) && k_faces[id].ported && s_enabled_cache[id] != 0 && s_nav_cache[id] != 0;
}

static int ordered_id_compare(faculty175_face_id_t a, faculty175_face_id_t b)
{
    if (!face_valid(a) || !face_valid(b)) {
        return (int)a - (int)b;
    }
    if (s_order_cache[a] != s_order_cache[b]) {
        return (int)s_order_cache[a] - (int)s_order_cache[b];
    }
    return (int)a - (int)b;
}

static void rebuild_navigation_map(void)
{
    s_nav_count = 0;
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        const faculty175_face_id_t id = k_faces[i].id;
        if (!face_nav_visible_cached(id)) {
            continue;
        }
        size_t pos = s_nav_count;
        while (pos > 0 && ordered_id_compare(id, s_nav_map[pos - 1u]) < 0) {
            s_nav_map[pos] = s_nav_map[pos - 1u];
            --pos;
        }
        s_nav_map[pos] = id;
        ++s_nav_count;
    }
}

static bool navigation_map_contains(faculty175_face_id_t id)
{
    for (size_t i = 0; i < s_nav_count; ++i) {
        if (s_nav_map[i] == id) {
            return true;
        }
    }
    return false;
}

static bool navigation_map_valid(void)
{
    size_t expected = 0;
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        if (face_nav_visible_cached(k_faces[i].id)) {
            ++expected;
        }
    }
    if (s_nav_count == 0 || s_nav_count != expected) {
        return false;
    }
    for (size_t i = 0; i < s_nav_count; ++i) {
        const faculty175_face_id_t id = s_nav_map[i];
        if (!face_nav_visible_cached(id)) {
            return false;
        }
        for (size_t j = i + 1u; j < s_nav_count; ++j) {
            if (s_nav_map[j] == id) {
                return false;
            }
        }
        if (i > 0 && ordered_id_compare(s_nav_map[i - 1u], id) >= 0) {
            return false;
        }
    }
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        const faculty175_face_id_t id = k_faces[i].id;
        if (face_nav_visible_cached(id) && !navigation_map_contains(id)) {
            return false;
        }
    }
    return true;
}

static esp_err_t persist_navigation_map_to_open_nvs(nvs_handle_t nvs)
{
    uint8_t ids[FACULTY175_FACE_COUNT] = {};
    for (size_t i = 0; i < s_nav_count; ++i) {
        ids[i] = (uint8_t)s_nav_map[i];
    }
    esp_err_t err = nvs_set_u8(nvs, FACES_NVS_NAV_COUNT, (uint8_t)s_nav_count);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, FACES_NVS_NAV_MAP, ids, s_nav_count);
    }
    return err;
}

static bool load_navigation_map_from_open_nvs(nvs_handle_t nvs)
{
    uint8_t count = 0;
    if (nvs_get_u8(nvs, FACES_NVS_NAV_COUNT, &count) != ESP_OK || count == 0 ||
        count > FACULTY175_FACE_COUNT) {
        return false;
    }
    uint8_t ids[FACULTY175_FACE_COUNT] = {};
    size_t len = count;
    if (nvs_get_blob(nvs, FACES_NVS_NAV_MAP, ids, &len) != ESP_OK || len != count) {
        return false;
    }
    s_nav_count = count;
    for (size_t i = 0; i < s_nav_count; ++i) {
        s_nav_map[i] = (faculty175_face_id_t)ids[i];
    }
    if (navigation_map_valid()) {
        return true;
    }
    s_nav_count = 0;
    return false;
}

static esp_err_t set_face_u8_and_commit_map(faculty175_face_id_t id, void (*key_fn)(const faculty175_face_desc_t *, char *, size_t),
                                            uint8_t *cache, uint8_t value)
{
    if (!face_valid(id) || key_fn == NULL || cache == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    char key[FACE_KEY_CAP];
    const uint8_t previous = cache[id];
    key_fn(&k_faces[id], key, sizeof(key));
    err = nvs_set_u8(nvs, key, value);
    if (err == ESP_OK) {
        cache[id] = value;
        rebuild_navigation_map();
        err = persist_navigation_map_to_open_nvs(nvs);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (err != ESP_OK) {
        cache[id] = previous;
        rebuild_navigation_map();
    }
    nvs_close(nvs);
    return err;
}

static ssize_t navigation_map_index_of(faculty175_face_id_t id)
{
    for (size_t i = 0; i < s_nav_count; ++i) {
        if (s_nav_map[i] == id) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static const faculty175_face_desc_t *next_enabled_from(faculty175_face_id_t from, int delta)
{
    if (s_nav_count > 0 && delta != 0) {
        ssize_t current = navigation_map_index_of(from);
        if (current < 0) {
            return &k_faces[s_nav_map[0]];
        }
        const size_t step = (size_t)(delta > 0 ? delta : -delta) % s_nav_count;
        const size_t next = delta >= 0 ? ((size_t)current + step) % s_nav_count
                                       : ((size_t)current + s_nav_count - step) % s_nav_count;
        return &k_faces[s_nav_map[next]];
    }
    const faculty175_face_desc_t *best = NULL;
    for (size_t pass = 0; pass < 2 && best == NULL; ++pass) {
        for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
            const faculty175_face_desc_t *candidate = &k_faces[i];
            if (!faculty175_faces_navigation_enabled(candidate->id)) {
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

static const faculty175_face_desc_t *next_enabled_in_category_from(faculty175_face_id_t from, int delta)
{
    if (!face_valid(from)) {
        return NULL;
    }
    const uint32_t category = primary_face_category(k_faces[from].categories);
    if (category == 0) {
        return NULL;
    }
    if (s_nav_count > 0 && delta != 0) {
        ssize_t current = navigation_map_index_of(from);
        if (current < 0) {
            current = 0;
        }
        for (size_t step = 1; step <= s_nav_count; ++step) {
            const size_t next = delta >= 0 ? ((size_t)current + step) % s_nav_count
                                           : ((size_t)current + s_nav_count - (step % s_nav_count)) % s_nav_count;
            const faculty175_face_desc_t *candidate = &k_faces[s_nav_map[next]];
            if (candidate->id != from && primary_face_category(candidate->categories) == category) {
                return candidate;
            }
        }
        return NULL;
    }
    const faculty175_face_desc_t *best = NULL;
    for (size_t pass = 0; pass < 2 && best == NULL; ++pass) {
        for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
            const faculty175_face_desc_t *candidate = &k_faces[i];
            if (candidate->id == from || primary_face_category(candidate->categories) != category ||
                !faculty175_faces_navigation_enabled(candidate->id)) {
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
        err = nvs_set_str(nvs, FACES_NVS_CURRENT, k_faces[FACULTY175_FACE_IRONMAN].slug);
        current[0] = '\0';
    }
    uint8_t schema = 0;
    if (err == ESP_OK && nvs_get_u8(nvs, FACES_NVS_SCHEMA, &schema) == ESP_ERR_NVS_NOT_FOUND) {
        schema = 0;
    }
    if (err == ESP_OK) {
        erase_legacy_face_keys_from_open_nvs(nvs);
    }

    for (size_t i = 0; i < FACULTY175_FACE_COUNT && err == ESP_OK; ++i) {
        char key[FACE_KEY_CAP];
        uint8_t value = 0;
        enabled_key(&k_faces[i], key, sizeof(key));
        if (nvs_get_u8(nvs, key, &value) == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_set_u8(nvs, key, k_faces[i].enabled_by_default ? 1 : 0);
            if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
                err = ESP_OK;
            }
        }
        order_key(&k_faces[i], key, sizeof(key));
        if (err == ESP_OK && nvs_get_u8(nvs, key, &value) == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_set_u8(nvs, key, k_faces[i].default_order);
            if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
                err = ESP_OK;
            }
        }
        nav_key(&k_faces[i], key, sizeof(key));
        if (err == ESP_OK && nvs_get_u8(nvs, key, &value) == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_set_u8(nvs, key, face_default_navigation_enabled(&k_faces[i]) ? 1 : 0);
            if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
                err = ESP_OK;
            }
        }
    }
    if (err == ESP_OK && schema < FACES_CONFIG_RESET_SCHEMA_VERSION) {
        for (size_t i = 0; i < FACULTY175_FACE_COUNT && err == ESP_OK; ++i) {
            if (!k_faces[i].ported) {
                continue;
            }
            char key[FACE_KEY_CAP];
            enabled_key(&k_faces[i], key, sizeof(key));
            err = nvs_set_u8(nvs, key, k_faces[i].enabled_by_default ? 1 : 0);
            if (err == ESP_OK) {
                order_key(&k_faces[i], key, sizeof(key));
                err = nvs_set_u8(nvs, key, k_faces[i].default_order);
            }
            if (err == ESP_OK) {
                nav_key(&k_faces[i], key, sizeof(key));
                err = nvs_set_u8(nvs, key, face_default_navigation_enabled(&k_faces[i]) ? 1 : 0);
            }
        }
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, FACES_NVS_CURRENT, k_faces[FACULTY175_FACE_IRONMAN].slug);
            if (err == ESP_OK) {
                strncpy(current, k_faces[FACULTY175_FACE_IRONMAN].slug, sizeof(current) - 1u);
                current[sizeof(current) - 1u] = '\0';
            }
        }
    }
    if (err == ESP_OK && schema < FACES_SCHEMA_VERSION) {
        esp_err_t schema_err = nvs_set_u8(nvs, FACES_NVS_SCHEMA, FACES_SCHEMA_VERSION);
        if (schema_err != ESP_OK && schema_err != ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
            err = schema_err;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (err == ESP_OK) {
        refresh_faces_cache_from_open_nvs(nvs);
        if (!load_navigation_map_from_open_nvs(nvs)) {
            rebuild_navigation_map();
            err = persist_navigation_map_to_open_nvs(nvs);
            if (err == ESP_OK) {
                err = nvs_commit(nvs);
            }
            if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
                FACULTY175_LOG_STAGE(TAG,
                                     "faces",
                                     "nav map using RAM only: %s",
                                     esp_err_to_name(err));
                err = ESP_OK;
            }
        }
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    s_current = FACULTY175_FACE_IRONMAN;
    const faculty175_face_desc_t *saved_current = faculty175_faces_find(current);
    if (saved_current != NULL && saved_current->ported && faculty175_faces_enabled(saved_current->id)) {
        s_current = saved_current->id;
    }
    FACULTY175_LOG_STAGE(TAG, "faces", "current=%s", faculty175_faces_current()->slug);
    const esp_err_t profile_err = faculty175_face_profile_init();
    if (profile_err != ESP_OK) {
        FACULTY175_LOG_STAGE(TAG, "faces", "profile init: %s", esp_err_to_name(profile_err));
    }
    faculty175_face_alethiometer_init();
    faculty175_face_runes_init();
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
    if (strcasecmp(slug, "ironman") == 0) {
        slug = "arc-reactor";
    }
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        if (!face_active_slot(i)) {
            continue;
        }
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
    if (!k_faces[id].ported) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!faculty175_faces_enabled(id)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_current = id;
    FACULTY175_LOG_STAGE(TAG, "faces", "set %s%s", k_faces[id].slug, k_faces[id].ported ? "" : " (not ported)");
    return ESP_OK;
}

static esp_err_t persist_current_face(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, FACES_NVS_CURRENT, k_faces[id].slug);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        err = faculty175_face_runes_save();
    }
    return err;
}

esp_err_t faculty175_faces_save_current(void)
{
    return persist_current_face(s_current);
}

esp_err_t faculty175_faces_set(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!k_faces[id].ported || !faculty175_faces_enabled(id)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Persist before publishing s_current. USB Screen changes can immediately
     * re-enumerate the native USB peripheral, and some hosts reset the S3 when
     * Serial/JTAG returns. The selected non-USB face must already be durable. */
    const esp_err_t err = persist_current_face(id);
    return err == ESP_OK ? faculty175_faces_set_runtime(id) : err;
}

esp_err_t faculty175_faces_set_enabled(faculty175_face_id_t id, bool enabled)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!k_faces[id].ported && enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (face_is_nav_anchor(id) && !enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = set_face_u8_and_commit_map(id, enabled_key, s_enabled_cache, enabled ? 1 : 0);
    if (err == ESP_OK && !enabled && s_current == id) {
        const faculty175_face_desc_t *next = faculty175_faces_nav_at(0);
        s_current = next != NULL ? next->id : FACULTY175_FACE_FACULTY;
    }
    return err;
}

esp_err_t faculty175_faces_set_navigation_enabled(faculty175_face_id_t id, bool enabled)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!k_faces[id].ported && enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (face_is_nav_anchor(id) && !enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = set_face_u8_and_commit_map(id, nav_key, s_nav_cache, enabled ? 1 : 0);
    if (err == ESP_OK && !enabled && s_current == id) {
        const faculty175_face_desc_t *next = faculty175_faces_nav_at(0);
        s_current = next != NULL ? next->id : FACULTY175_FACE_POCKETWATCH;
    }
    return err;
}

esp_err_t faculty175_faces_set_category_navigation_enabled(uint32_t category, bool enabled)
{
    if (category == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FACES_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    bool current_hidden = false;
    bool changed = false;
    uint8_t previous_nav[FACULTY175_FACE_COUNT];
    memcpy(previous_nav, s_nav_cache, sizeof(previous_nav));

    const uint8_t value = enabled ? 1 : 0;
    for (size_t i = 0; i < FACULTY175_FACE_COUNT && err == ESP_OK; ++i) {
        if ((k_faces[i].categories & category) == 0) {
            continue;
        }
        if (!k_faces[i].ported) {
            continue;
        }
        if (!enabled && face_is_nav_anchor(k_faces[i].id)) {
            continue;
        }

        char key[FACE_KEY_CAP];
        nav_key(&k_faces[i], key, sizeof(key));
        err = nvs_set_u8(nvs, key, value);
        if (err == ESP_OK) {
            changed = true;
            s_nav_cache[k_faces[i].id] = value;
            current_hidden = current_hidden || (!enabled && s_current == k_faces[i].id);
        }
    }

    if (err == ESP_OK && changed) {
        rebuild_navigation_map();
        err = persist_navigation_map_to_open_nvs(nvs);
    }
    if (err == ESP_OK && changed) {
        err = nvs_commit(nvs);
    }
    if (err != ESP_OK) {
        memcpy(s_nav_cache, previous_nav, sizeof(previous_nav));
        rebuild_navigation_map();
    }
    nvs_close(nvs);

    if (err == ESP_OK && current_hidden) {
        const faculty175_face_desc_t *next = faculty175_faces_nav_at(0);
        s_current = next != NULL ? next->id : FACULTY175_FACE_POCKETWATCH;
    }
    return err;
}

esp_err_t faculty175_faces_set_order(faculty175_face_id_t id, uint8_t order)
{
    if (!face_valid(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    return set_face_u8_and_commit_map(id, order_key, s_order_cache, order);
}

bool faculty175_faces_enabled(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return false;
    }
    if (!k_faces[id].ported) {
        return false;
    }
    if (s_faces_cache_loaded) {
        return s_enabled_cache[id] != 0;
    }
    char key[FACE_KEY_CAP];
    enabled_key(&k_faces[id], key, sizeof(key));
    uint8_t enabled = k_faces[id].enabled_by_default ? 1 : 0;
    (void)nvs_get_u8_or_default(key, enabled, &enabled);
    return enabled != 0;
}

bool faculty175_faces_navigation_enabled(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return false;
    }
    if (!k_faces[id].ported) {
        return false;
    }
    if (s_faces_cache_loaded) {
        return s_enabled_cache[id] != 0 && s_nav_cache[id] != 0;
    }
    if (!faculty175_faces_enabled(id)) {
        return false;
    }
    char key[FACE_KEY_CAP];
    nav_key(&k_faces[id], key, sizeof(key));
    uint8_t enabled = face_default_navigation_enabled(&k_faces[id]) ? 1 : 0;
    (void)nvs_get_u8_or_default(key, enabled, &enabled);
    return enabled != 0;
}

uint8_t faculty175_faces_order(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return 255;
    }
    if (s_faces_cache_loaded) {
        return s_order_cache[id];
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

const faculty175_face_desc_t *faculty175_faces_cycle_vertical_runtime(int delta)
{
    const faculty175_face_desc_t *next = next_enabled_in_category_from(s_current, delta);
    if (next != NULL) {
        (void)faculty175_faces_set_runtime(next->id);
    }
    return faculty175_faces_current();
}

const faculty175_face_desc_t *faculty175_faces_nav_at(size_t index)
{
    if (index < s_nav_count) {
        return &k_faces[s_nav_map[index]];
    }
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        const faculty175_face_desc_t *candidate = &k_faces[i];
        if (!faculty175_faces_navigation_enabled(candidate->id)) {
            continue;
        }
        size_t before = 0;
        for (size_t j = 0; j < FACULTY175_FACE_COUNT; ++j) {
            const faculty175_face_desc_t *other = &k_faces[j];
            if (faculty175_faces_navigation_enabled(other->id) && ordered_compare(other, candidate) < 0) {
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
    size_t count = 0;
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        if (face_active_slot(i)) {
            ++count;
        }
    }
    return count;
}

size_t faculty175_faces_enabled_count(void)
{
    if (s_nav_count > 0) {
        return s_nav_count;
    }
    size_t count = 0;
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        if (faculty175_faces_navigation_enabled(k_faces[i].id)) {
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

    if (s_nav_count > 0) {
        const ssize_t index = navigation_map_index_of(s_current);
        *out_index = index >= 0 ? (size_t)index : 0;
        *out_count = s_nav_count;
        return true;
    }

    size_t index = 0;
    size_t count = 0;
    const faculty175_face_desc_t *current = &k_faces[s_current];
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
        const faculty175_face_desc_t *candidate = &k_faces[i];
        if (!faculty175_faces_navigation_enabled(candidate->id)) {
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

bool faculty175_faces_vertical_group(faculty175_face_id_t id)
{
    if (!face_valid(id)) {
        return false;
    }
    return next_enabled_in_category_from(id, 1) != NULL;
}

static void print_face_line(const faculty175_face_desc_t *face)
{
    printf("face: %-12s enabled=%s nav=%s order=%u ported=%s categories=0x%02lx%s\n",
           face->slug,
           faculty175_faces_enabled(face->id) ? "yes" : "no",
           faculty175_faces_navigation_enabled(face->id) ? "yes" : "no",
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
        printf("faces: current=%s profile=%s count=%u\n",
               faculty175_faces_current()->slug,
               faculty175_face_profile_slug(faculty175_face_profile_current()),
               (unsigned)faculty175_faces_count());
        for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
            if (!face_active_slot(i)) {
                continue;
            }
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
        printf("  faces nav <slug> <0|1>\n");
        printf("  faces show <slug>\n");
        printf("  faces hide <slug>\n");
        printf("  faces group <home|common|oracle|instrument|system> <0|1>\n");
        printf("  faces order <slug> <0-255>\n");
        printf("  faces profile status\n");
        printf("  faces profile default|secops|fortune|castalia|ocarina|lunasay|cameo\n");
        printf("  pocketwatch dial: 123 fortune, 443 secops, 175 castalia, 145 ocarina, 295 lunasay, 987 cameo, 121110 default\n");
        fflush(stdout);
        return true;
    }

    if (strcasecmp(cmd, "next") == 0 || strcasecmp(cmd, "prev") == 0) {
        const faculty175_face_desc_t *next = faculty175_faces_cycle(strcasecmp(cmd, "next") == 0 ? 1 : -1);
        printf("faces: current=%s\n", next != NULL ? next->slug : "-");
        fflush(stdout);
        return true;
    }

    if (strcasecmp(cmd, "group") == 0) {
        const uint32_t category = category_from_name(slug);
        const esp_err_t err = value <= 1 ? faculty175_faces_set_category_navigation_enabled(category, value != 0)
                                         : ESP_ERR_INVALID_ARG;
        printf("faces: group %s nav=%u %s\n", slug, value, esp_err_to_name(err));
        fflush(stdout);
        return true;
    }

    if (strcasecmp(cmd, "profile") == 0) {
        if (slug[0] == '\0' || strcasecmp(slug, "status") == 0) {
            printf("faces: profile=%s\n", faculty175_face_profile_slug(faculty175_face_profile_current()));
        } else {
            faculty175_face_profile_t profile;
            if (!faculty175_face_profile_from_slug(slug, &profile)) {
                printf("faces: unknown profile \"%s\" (default|secops|fortune|castalia|ocarina|lunasay|cameo)\n", slug);
            } else {
                const esp_err_t err = faculty175_face_profile_apply(profile, true);
                printf("faces: profile %s %s\n", slug, esp_err_to_name(err));
            }
        }
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
    } else if (strcasecmp(cmd, "nav") == 0) {
        err = value <= 1 ? faculty175_faces_set_navigation_enabled(face->id, value != 0) : ESP_ERR_INVALID_ARG;
    } else if (strcasecmp(cmd, "show") == 0) {
        err = faculty175_faces_set_navigation_enabled(face->id, true);
    } else if (strcasecmp(cmd, "hide") == 0) {
        err = faculty175_faces_set_navigation_enabled(face->id, false);
    } else if (strcasecmp(cmd, "order") == 0) {
        err = value <= 255 ? faculty175_faces_set_order(face->id, (uint8_t)value) : ESP_ERR_INVALID_ARG;
    }
    printf("faces: %s %s %s\n", cmd, face->slug, esp_err_to_name(err));
    fflush(stdout);
    return true;
}
