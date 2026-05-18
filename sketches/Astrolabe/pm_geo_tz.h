#pragma once

#include <stdint.h>

/** Seconds to add to UTC `time_t` for local civil time (from last IP lookup). */
int32_t pm_geo_tz_offset_sec();

/** HTTP lookup (worldtimeapi.org, then ip-api.com). Requires WiFi. No-op if both fail. */
bool pm_geo_tz_refresh_from_ip();
