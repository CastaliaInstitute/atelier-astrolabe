#pragma once

#include <stdbool.h>
#include <time.h>

#include "faculty175_charts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Low-memory geocentric ecliptic longitudes shared by firmware and WebSim. */
bool faculty175_astro_positions_at_utc(const struct tm *utc, faculty175_chart_positions_t *out);
bool faculty175_astro_positions_at_epoch(time_t epoch, faculty175_chart_positions_t *out);
bool faculty175_astro_slow_positions_at_epoch(time_t epoch,
                                              double *uranus_lon,
                                              double *neptune_lon,
                                              double *pluto_lon,
                                              double *mean_node_lon);

#ifdef __cplusplus
}
#endif
