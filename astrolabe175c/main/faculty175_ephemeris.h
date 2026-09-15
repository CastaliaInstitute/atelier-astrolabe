#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACULTY175_HD_BODY_SUN = 0,
    FACULTY175_HD_BODY_EARTH,
    FACULTY175_HD_BODY_MOON,
    FACULTY175_HD_BODY_MERCURY,
    FACULTY175_HD_BODY_VENUS,
    FACULTY175_HD_BODY_MARS,
    FACULTY175_HD_BODY_JUPITER,
    FACULTY175_HD_BODY_SATURN,
    FACULTY175_HD_BODY_URANUS,
    FACULTY175_HD_BODY_NEPTUNE,
    FACULTY175_HD_BODY_PLUTO,
    FACULTY175_HD_BODY_TRUE_NODE,
    FACULTY175_HD_BODY_SOUTH_NODE,
    FACULTY175_HD_BODY_COUNT,
} faculty175_hd_body_t;

typedef struct {
    double lon[FACULTY175_HD_BODY_COUNT];
    bool ok;
    bool from_network;
} faculty175_hd_positions_t;

bool faculty175_ephemeris_fetch_human_design_epoch(time_t utc_epoch, faculty175_hd_positions_t *out);
const char *faculty175_ephemeris_hd_body_label(faculty175_hd_body_t body);

#ifdef __cplusplus
}
#endif
