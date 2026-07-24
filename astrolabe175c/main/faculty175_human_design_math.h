#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Map tropical ecliptic longitude to the standard Rave Mandala gate and line. */
bool faculty175_human_design_gate_line(double longitude_deg, uint8_t *gate, uint8_t *line);

/** Find the instant when the Sun was exactly 88 degrees behind its birth longitude. */
bool faculty175_human_design_design_epoch(time_t birth_epoch, time_t *design_epoch);

#ifdef __cplusplus
}
#endif
