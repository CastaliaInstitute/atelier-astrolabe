#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    FACULTY175_FACE_FACULTY = 0,
    FACULTY175_FACE_CLASSIC,
    FACULTY175_FACE_APOCALYPSO,
    FACULTY175_FACE_DIGITAL,
    FACULTY175_FACE_SPOTIFY,
    FACULTY175_FACE_NOTES,
    FACULTY175_FACE_MOON,
    FACULTY175_FACE_CALCIFER,
    FACULTY175_FACE_CASTALIA,
    FACULTY175_FACE_ASTROLOGY,
    FACULTY175_FACE_SYNASTRY,
    FACULTY175_FACE_TAROT,
    FACULTY175_FACE_INQ,
    FACULTY175_FACE_RUNES,
    FACULTY175_FACE_ALETHIOMETER,
    FACULTY175_FACE_SPECTRUM,
    FACULTY175_FACE_CHAKRA,
    FACULTY175_FACE_BOWL,
    FACULTY175_FACE_ROCKET,
    FACULTY175_FACE_RADAR,
    FACULTY175_FACE_WEATHER,
    FACULTY175_FACE_GLOBE,
    FACULTY175_FACE_SCALE,
    FACULTY175_FACE_ALMANAC,
    FACULTY175_FACE_SKY,
    FACULTY175_FACE_QUOTES,
    FACULTY175_FACE_TRANSITS,
    FACULTY175_FACE_OCARINA,
    FACULTY175_FACE_PITCH,
    FACULTY175_FACE_BONGO,
    FACULTY175_FACE_PIANO,
    FACULTY175_FACE_KALIMBA,
    FACULTY175_FACE_DRONE,
    FACULTY175_FACE_CHORD,
    FACULTY175_FACE_LEVEL,
    FACULTY175_FACE_TUNING,
    FACULTY175_FACE_PANDRUM,
    FACULTY175_FACE_ORIENT,
    FACULTY175_FACE_LUOPAN,
    FACULTY175_FACE_QDAY,
    FACULTY175_FACE_FOCUS,
    FACULTY175_FACE_BIOMETRICS,
    FACULTY175_FACE_WATCHER,
    FACULTY175_FACE_LENORMAND,
    FACULTY175_FACE_PYTHIA,
    FACULTY175_FACE_GEOMANCY,
    FACULTY175_FACE_ENOCHIAN,
    FACULTY175_FACE_HID,
    FACULTY175_FACE_BABEL,
    FACULTY175_FACE_SETTINGS,
    FACULTY175_FACE_COUNT,
} faculty175_face_id_t;

typedef enum {
    FACULTY175_FACE_CAT_HOME = 1u << 0,
    FACULTY175_FACE_CAT_COMMONPLACE = 1u << 1,
    FACULTY175_FACE_CAT_ORACLE = 1u << 2,
    FACULTY175_FACE_CAT_INSTRUMENT = 1u << 3,
    FACULTY175_FACE_CAT_SYSTEM = 1u << 4,
} faculty175_face_category_t;

typedef struct {
    faculty175_face_id_t id;
    const char *slug;
    const char *label;
    uint32_t categories;
    bool ported;
    bool enabled_by_default;
    uint8_t default_order;
} faculty175_face_desc_t;

esp_err_t faculty175_faces_init(void);
const faculty175_face_desc_t *faculty175_faces_current(void);
const faculty175_face_desc_t *faculty175_faces_find(const char *slug);
const faculty175_face_desc_t *faculty175_faces_get(faculty175_face_id_t id);
esp_err_t faculty175_faces_set(faculty175_face_id_t id);
esp_err_t faculty175_faces_set_runtime(faculty175_face_id_t id);
esp_err_t faculty175_faces_save_current(void);
esp_err_t faculty175_faces_set_enabled(faculty175_face_id_t id, bool enabled);
esp_err_t faculty175_faces_set_order(faculty175_face_id_t id, uint8_t order);
bool faculty175_faces_enabled(faculty175_face_id_t id);
uint8_t faculty175_faces_order(faculty175_face_id_t id);
const faculty175_face_desc_t *faculty175_faces_cycle(int delta);
const faculty175_face_desc_t *faculty175_faces_cycle_runtime(int delta);
const faculty175_face_desc_t *faculty175_faces_nav_at(size_t index);
size_t faculty175_faces_count(void);
size_t faculty175_faces_enabled_count(void);
bool faculty175_faces_nav_position(size_t *out_index, size_t *out_count);
bool faculty175_faces_handle(const char *line);
