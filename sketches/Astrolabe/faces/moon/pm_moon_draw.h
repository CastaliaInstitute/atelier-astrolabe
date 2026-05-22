#pragma once

#include <stdbool.h>

#include "pm_transit.h"

class Arduino_Canvas;

/** Sun–Moon elongation [0,360), illuminated fraction, waxing vs waning. */
bool pm_moon_phase_from_transit(const PmTransitPositions *tp, float *illum, bool *waxing,
                                double *elong_deg = nullptr);

/** Northern-hemisphere view: +x is east; waxing lights the right limb. */
bool pm_moon_point_lit(int dx, int screen_r, float illum, bool waxing);
/** Curved terminator variant for actual disk pixels. */
bool pm_moon_point_lit_curved(int dx, int dy, int screen_r, float illum, bool waxing);

/** Photo disk + terminator; `show_rim` optional 1px outline. */
void pm_moon_draw_disk(Arduino_Canvas *gfx, int cx, int cy, int screen_r, float illum, bool waxing,
                       bool show_rim = false);
