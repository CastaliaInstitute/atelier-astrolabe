#pragma once

/**
 * Circadian 24h hue map for clock surfaces.
 *
 * Keyframes are in HSV degrees:
 *   00:00 -> 250 deg (indigo)
 *   06:00 ->  30 deg (warm dawn)
 *   12:00 -> 120 deg (daylight green)
 *   18:00 ->  30 deg (warm dusk)
 *   24:00 -> 250 deg (wrap to midnight)
 */
float pm_circadian_hue_from_seconds(float seconds_of_day);

