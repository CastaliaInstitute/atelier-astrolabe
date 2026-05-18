#pragma once

#include <Arduino.h>
#include <cstdint>

/** Load pulse settings from NVS (call once from setup). */
void pm_home_gem_pulse_begin(void);

bool pm_home_gem_pulse_enabled(void);
void pm_home_gem_pulse_set_enabled(bool on);

/** Beats per minute (20–120). Default ~resting meditative heartbeat. */
uint8_t pm_home_gem_pulse_bpm(void);
void pm_home_gem_pulse_set_bpm(uint8_t bpm);

/** Brightness multiplier for gem draw (≈0.86–1.0). */
float pm_home_gem_pulse_brightness(uint32_t now_ms);

/** Target interval between pulse frames while on home face. */
uint32_t pm_home_gem_pulse_repaint_interval_ms(void);

/**
 * Handle `gem …` serial lines. Returns true if `line` was a gem command.
 * Usage: gem pulse | gem pulse on|off | gem pulse bpm 52
 */
bool pm_home_gem_pulse_serial_command(const char *line);
