#include "pm_home_gem_pulse.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "pm_config.h"

namespace {

constexpr char kNs[] = "mynah";
constexpr char kKeyEn[] = "gem_pulse_en";
constexpr char kKeyBpm[] = "gem_pulse_bpm";

bool s_enabled = (MYNAH_HUE_GEM_PULSE_DEFAULT != 0);
uint8_t s_bpm = MYNAH_HUE_GEM_PULSE_BPM_DEFAULT;

float smoothstep01(float t) {
  if (t <= 0.f) {
    return 0.f;
  }
  if (t >= 1.f) {
    return 1.f;
  }
  return t * t * (3.f - 2.f * t);
}

/** 0..1 lub–dab style wave for one beat at `phase` in [0,1). */
float heartbeat_wave(float phase) {
  if (phase < 0.14f) {
    return smoothstep01(phase / 0.14f);
  }
  if (phase < 0.22f) {
    return 1.f - 0.28f * smoothstep01((phase - 0.14f) / 0.08f);
  }
  if (phase < 0.36f) {
    return 0.72f + 0.28f * smoothstep01((phase - 0.22f) / 0.14f);
  }
  return 0.72f * (1.f - smoothstep01((phase - 0.36f) / 0.64f));
}

void save_nvs(void) {
  Preferences pref;
  if (!pref.begin(kNs, false)) {
    return;
  }
  pref.putBool(kKeyEn, s_enabled);
  pref.putUChar(kKeyBpm, s_bpm);
  pref.end();
}

uint8_t clamp_bpm(int bpm) {
  if (bpm < static_cast<int>(MYNAH_HUE_GEM_PULSE_BPM_MIN)) {
    return MYNAH_HUE_GEM_PULSE_BPM_MIN;
  }
  if (bpm > static_cast<int>(MYNAH_HUE_GEM_PULSE_BPM_MAX)) {
    return MYNAH_HUE_GEM_PULSE_BPM_MAX;
  }
  return static_cast<uint8_t>(bpm);
}

}  // namespace

void pm_home_gem_pulse_begin(void) {
  Preferences pref;
  if (!pref.begin(kNs, true)) {
    return;
  }
  if (pref.isKey(kKeyEn)) {
    s_enabled = pref.getBool(kKeyEn, s_enabled);
  }
  if (pref.isKey(kKeyBpm)) {
    s_bpm = clamp_bpm(static_cast<int>(pref.getUChar(kKeyBpm, s_bpm)));
  }
  pref.end();
}

bool pm_home_gem_pulse_enabled(void) { return s_enabled; }

void pm_home_gem_pulse_set_enabled(bool on) {
  s_enabled = on;
  save_nvs();
}

uint8_t pm_home_gem_pulse_bpm(void) { return s_bpm; }

void pm_home_gem_pulse_set_bpm(uint8_t bpm) {
  s_bpm = clamp_bpm(bpm);
  save_nvs();
}

float pm_home_gem_pulse_brightness(uint32_t now_ms) {
  if (!s_enabled) {
    return 1.f;
  }
  const float period_ms = 60000.f / static_cast<float>(s_bpm);
  float phase = fmodf(static_cast<float>(now_ms) / period_ms, 1.f);
  if (phase < 0.f) {
    phase += 1.f;
  }
  const float wave = heartbeat_wave(phase);
  /** Subtle core swell; stronger toward center is applied in gem draw via radius. */
  return 1.f - MYNAH_HUE_GEM_PULSE_DEPTH * (1.f - wave);
}

uint32_t pm_home_gem_pulse_repaint_interval_ms(void) {
  if (!s_enabled) {
    return 1000u;
  }
  const uint32_t period = static_cast<uint32_t>(60000.f / static_cast<float>(s_bpm));
  uint32_t interval = period / 16u;
  if (interval < MYNAH_HUE_GEM_PULSE_REPAINT_MIN_MS) {
    interval = MYNAH_HUE_GEM_PULSE_REPAINT_MIN_MS;
  }
  if (interval > 120u) {
    interval = 120u;
  }
  return interval;
}

bool pm_home_gem_pulse_serial_command(const char *line) {
  if (strncmp(line, "gem ", 4) != 0) {
    return false;
  }
  const char *args = line + 4;
  while (*args == ' ') {
    ++args;
  }
  if (strncmp(args, "pulse", 5) != 0) {
    Serial.println("gem: usage: gem pulse [on|off|bpm N]");
    return true;
  }
  args += 5;
  while (*args == ' ') {
    ++args;
  }
  if (*args == '\0') {
    Serial.printf("gem pulse: %s bpm=%u (%.1fs/beat)\n", s_enabled ? "on" : "off", static_cast<unsigned>(s_bpm),
                  60.f / static_cast<float>(s_bpm));
    return true;
  }
  if (strcmp(args, "on") == 0 || strcmp(args, "1") == 0) {
    pm_home_gem_pulse_set_enabled(true);
    Serial.println("gem pulse: on");
    return true;
  }
  if (strcmp(args, "off") == 0 || strcmp(args, "0") == 0) {
    pm_home_gem_pulse_set_enabled(false);
    Serial.println("gem pulse: off");
    return true;
  }
  if (strncmp(args, "bpm ", 4) == 0) {
    const char *p = args + 4;
    while (*p == ' ') {
      ++p;
    }
    int bpm = 0;
    if (sscanf(p, "%d", &bpm) == 1) {
      pm_home_gem_pulse_set_bpm(clamp_bpm(bpm));
      Serial.printf("gem pulse: bpm=%u\n", static_cast<unsigned>(s_bpm));
    } else {
      Serial.printf("gem pulse: usage bpm %u-%u\n", static_cast<unsigned>(MYNAH_HUE_GEM_PULSE_BPM_MIN),
                    static_cast<unsigned>(MYNAH_HUE_GEM_PULSE_BPM_MAX));
    }
    return true;
  }
  if (strncmp(args, "rate ", 5) == 0) {
    const char *p = args + 5;
    while (*p == ' ') {
      ++p;
    }
    int bpm = 0;
    if (sscanf(p, "%d", &bpm) == 1) {
      pm_home_gem_pulse_set_bpm(clamp_bpm(bpm));
      Serial.printf("gem pulse: bpm=%u\n", static_cast<unsigned>(s_bpm));
    } else {
      Serial.printf("gem pulse: usage rate %u-%u\n", static_cast<unsigned>(MYNAH_HUE_GEM_PULSE_BPM_MIN),
                    static_cast<unsigned>(MYNAH_HUE_GEM_PULSE_BPM_MAX));
    }
    return true;
  }
  Serial.println("gem pulse: usage: [on|off|bpm N|rate N]");
  return true;
}
