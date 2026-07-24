#include "faculty175_human_design_math.h"

#include <math.h>

#include "faculty175_astro_math.h"

#define HD_RAVE_START_DEGREE 358.25
#define HD_DESIGN_SOLAR_ARC_DEGREES 88.0

static const uint8_t k_mandala_gate_order[64] = {
    25, 17, 21, 51, 42, 3, 27, 24, 2, 23, 8, 20, 16, 35, 45, 12,
    15, 52, 39, 53, 62, 56, 31, 33, 7, 4, 29, 59, 40, 64, 47, 6,
    46, 18, 48, 57, 32, 50, 28, 44, 1, 43, 14, 34, 9, 5, 26, 11,
    10, 58, 38, 54, 61, 60, 41, 19, 13, 49, 30, 55, 37, 63, 22, 36,
};

static double norm360(double value)
{
    value = fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

bool faculty175_human_design_gate_line(double longitude_deg, uint8_t *gate, uint8_t *line)
{
    if (!isfinite(longitude_deg) || gate == NULL || line == NULL) {
        return false;
    }
    const double gate_step = 360.0 / 64.0;
    const double line_step = gate_step / 6.0;
    const double adjusted = norm360(norm360(longitude_deg) - HD_RAVE_START_DEGREE);
    int slot = (int)floor(adjusted / gate_step);
    int line_index = (int)floor((adjusted - (double)slot * gate_step) / line_step);
    if (slot < 0) {
        slot = 0;
    } else if (slot > 63) {
        slot = 63;
    }
    if (line_index < 0) {
        line_index = 0;
    } else if (line_index > 5) {
        line_index = 5;
    }
    *gate = k_mandala_gate_order[slot];
    *line = (uint8_t)(line_index + 1);
    return true;
}

bool faculty175_human_design_design_epoch(time_t birth_epoch, time_t *design_epoch)
{
    if (birth_epoch <= 0 || design_epoch == NULL) {
        return false;
    }
    faculty175_chart_positions_t birth = {0};
    if (!faculty175_astro_positions_at_epoch(birth_epoch, &birth)) {
        return false;
    }

    time_t earlier = birth_epoch - (time_t)(100 * 86400);
    time_t later = birth_epoch - (time_t)(75 * 86400);
    for (int iteration = 0; iteration < 32 && later - earlier > 1; ++iteration) {
        const time_t candidate = earlier + (later - earlier) / 2;
        faculty175_chart_positions_t position = {0};
        if (!faculty175_astro_positions_at_epoch(candidate, &position)) {
            return false;
        }
        const double arc = norm360(birth.lon[0] - position.lon[0]);
        if (arc > HD_DESIGN_SOLAR_ARC_DEGREES) {
            earlier = candidate;
        } else {
            later = candidate;
        }
    }
    *design_epoch = earlier + (later - earlier) / 2;
    return true;
}
