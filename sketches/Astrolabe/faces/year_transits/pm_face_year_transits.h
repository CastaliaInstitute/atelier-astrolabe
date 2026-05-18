#pragma once

#include <stddef.h>
#include <ctime>

/** Draws a radial year calendar with named transit currents as concentric arcs. */
void pm_face_year_transits_draw(const struct tm *tm_local, bool valid_local);

/** Select the next/previous transit arc for highlight. Returns false when no arcs are available. */
bool pm_face_year_transits_cycle_selected(int delta);

/** Short label for the selected transit arc, suitable for gesture banners. */
bool pm_face_year_transits_selected_summary(char *buf, size_t cap);
