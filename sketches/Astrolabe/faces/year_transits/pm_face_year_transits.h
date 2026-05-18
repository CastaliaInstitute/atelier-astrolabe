#pragma once

#include <ctime>

/** Draws a radial year calendar with named transit currents as concentric arcs. */
void pm_face_year_transits_draw(const struct tm *tm_local, bool valid_local);
