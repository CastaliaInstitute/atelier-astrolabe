#pragma once

#include <Arduino.h>
#include <cstdint>

/** Load pulse settings from NVS (call once from setup). */
void pm_home_gem_pulse_begin(void);

bool pm_home_gem_pulse_enabled(void);
void pm_home_gem_pulse_set_enabled(bool on);

/** Full 5-6-7 breath cycles per minute (2–8). */
uint8_t pm_home_gem_pulse_bpm(void);
void pm_home_gem_pulse_set_bpm(uint8_t bpm);

/** Brightness multiplier for gem draw (uniform across disc). */
float pm_home_gem_pulse_brightness(uint32_t now_ms);

/** 5-6-7 breath phase 0 = exhale trough, 1 = inhale/hold peak. */
float pm_home_gem_pulse_breath_amount(uint32_t now_ms);

/** Target interval between pulse frames while on home face. */
uint32_t pm_home_gem_pulse_repaint_interval_ms(void);

/**
 * Handle `gem …` serial lines. Returns true if `line` was a gem command.
 * Usage: gem pulse | gem pulse on|off | gem pulse bpm 52
 */
bool pm_home_gem_pulse_serial_command(const char *line);
