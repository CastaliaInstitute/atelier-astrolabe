#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "faculty175_face_profile.h"

typedef enum {
    FACULTY175_VARIANT_POCKET = 0,
    FACULTY175_VARIANT_ASTROLABE = 1,
    FACULTY175_VARIANT_LUNASAY = 2,
    FACULTY175_VARIANT_OCARINA = 3,
    FACULTY175_VARIANT_CAMEO = 4,
    FACULTY175_VARIANT_LUOPAN = 5,
    FACULTY175_VARIANT_ENSO = 6,
    FACULTY175_VARIANT_SMART_SPEAKER = 7,
    FACULTY175_VARIANT_BABEL_FISH = 8,
    FACULTY175_VARIANT_COUNT = 9,
} faculty175_variant_t;

bool faculty175_variant_profile_from_nvs(faculty175_face_profile_t *out, bool *found_out);
bool faculty175_variant_from_profile(faculty175_face_profile_t profile, faculty175_variant_t *out);
bool faculty175_variant_profile_from_slug(const char *slug, faculty175_face_profile_t *out);
esp_err_t faculty175_variant_persist_for_profile(faculty175_face_profile_t profile);
const char *faculty175_variant_label(faculty175_variant_t variant);
const char *faculty175_variant_ota_channel(faculty175_variant_t variant);
