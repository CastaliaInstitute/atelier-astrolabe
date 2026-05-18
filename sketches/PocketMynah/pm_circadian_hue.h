#pragma once

/**
 * Circadian 24h hue map for clock surfaces (solar loop, not raw HSV rainbow).
 *
 * Twelve keyframes from midnight indigo through dawn rose/gold, noon green,
 * afternoon cyan/blue, and evening violet — interpolated with shortest-path hue.
 */
float pm_circadian_hue_from_seconds(float seconds_of_day);
