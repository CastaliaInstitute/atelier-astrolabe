#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_faces.h"

typedef enum {
    FACULTY175_NATIVE_ANALOG = 0,
    FACULTY175_NATIVE_DIGITAL,
    FACULTY175_NATIVE_ORACLE,
    FACULTY175_NATIVE_INSTRUMENT,
    FACULTY175_NATIVE_CELESTIAL,
    FACULTY175_NATIVE_RADAR,
    FACULTY175_NATIVE_STATUS,
    FACULTY175_NATIVE_TEXT,
} faculty175_native_style_t;

typedef struct {
    faculty175_face_id_t id;
    const char *title;
    const char *subtitle;
    faculty175_native_style_t style;
    uint8_t hue;
    const char *a;
    const char *b;
    const char *c;
} faculty175_native_face_t;

void faculty175_face_native_draw(const faculty175_native_face_t *face, uint32_t anim_ms);
bool faculty175_face_native_action(faculty175_face_id_t id, uint32_t seed_ms);
bool faculty175_face_native_audio_busy(void);
bool faculty175_face_native_chakra_delta(faculty175_face_id_t id, int delta);
const char *faculty175_face_native_chakra_name(void);
