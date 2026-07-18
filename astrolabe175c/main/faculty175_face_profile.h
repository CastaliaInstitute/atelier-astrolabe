#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "faculty175_faces.h"

typedef enum {
    FACULTY175_FACE_PROFILE_DEFAULT = 0,
    FACULTY175_FACE_PROFILE_SECOPS = 1,
    FACULTY175_FACE_PROFILE_FORTUNE = 2,
    FACULTY175_FACE_PROFILE_CASTALIA = 3,
    FACULTY175_FACE_PROFILE_OCARINA = 4,
    FACULTY175_FACE_PROFILE_LUNASAY = 5,
    FACULTY175_FACE_PROFILE_CAMEO = 6,
    FACULTY175_FACE_PROFILE_CYBER = 7,
} faculty175_face_profile_t;

esp_err_t faculty175_face_profile_init(void);
esp_err_t faculty175_face_profile_apply(faculty175_face_profile_t profile, bool persist);
faculty175_face_profile_t faculty175_face_profile_current(void);
const char *faculty175_face_profile_slug(faculty175_face_profile_t profile);
const char *faculty175_face_profile_label(faculty175_face_profile_t profile);
bool faculty175_face_profile_from_slug(const char *slug, faculty175_face_profile_t *out);
faculty175_face_id_t faculty175_face_profile_home_face(faculty175_face_profile_t profile);
bool faculty175_face_profile_face_allowed(faculty175_face_profile_t profile, faculty175_face_id_t id);
