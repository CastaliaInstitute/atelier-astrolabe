#include "pm_home_gem_pulse.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "pm_config.h"
#include "pm_nvs.h"

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

float beat_phase(uint32_t now_ms) {
  const float period_ms = 60000.f / static_cast<float>(s_bpm);
  float phase = fmodf(static_cast<float>(now_ms) / period_ms, 1.f);
  if (phase < 0.f) {
    phase += 1.f;
  }
  return phase;
}

/** 5-6-7 pranayama: inhale → hold → exhale (smoothstep segments). */
float breath_amount(uint32_t now_ms) {
  if (!s_enabled) {
    return 0.5f;
  }
  const float phase = beat_phase(now_ms);
  const float k_total = static_cast<float>(MYNAH_HUE_GEM_PULSE_INHALE + MYNAH_HUE_GEM_PULSE_HOLD +
                                           MYNAH_HUE_GEM_PULSE_EXHALE);
  const float p = phase * k_total;
  const float t_in = static_cast<float>(MYNAH_HUE_GEM_PULSE_INHALE);
  const float t_hold = t_in + static_cast<float>(MYNAH_HUE_GEM_PULSE_HOLD);

  if (p < t_in) {
    return smoothstep01(p / t_in);
  }
  if (p < t_hold) {
    return 1.f;
  }
  return 1.f - smoothstep01((p - t_hold) / static_cast<float>(MYNAH_HUE_GEM_PULSE_EXHALE));
}

void save_nvs(void) {
  (void)pm_nvs_set_bool(kNs, kKeyEn, s_enabled);
  (void)pm_nvs_set_u8(kNs, kKeyBpm, s_bpm);
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
  if (pm_nvs_has_key(kNs, kKeyEn)) {
    s_enabled = pm_nvs_get_bool(kNs, kKeyEn, s_enabled);
  }
  if (pm_nvs_has_key(kNs, kKeyBpm)) {
    s_bpm = clamp_bpm(static_cast<int>(pm_nvs_get_u8(kNs, kKeyBpm, s_bpm)));
  }
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

float pm_home_gem_pulse_breath_amount(uint32_t now_ms) { return breath_amount(now_ms); }

float pm_home_gem_pulse_brightness(uint32_t now_ms) {
  if (!s_enabled) {
    return 1.f;
  }
  const float breath = breath_amount(now_ms);
  return 1.f - MYNAH_HUE_GEM_PULSE_DEPTH * (1.f - breath);
}

uint32_t pm_home_gem_pulse_repaint_interval_ms(void) {
  if (!s_enabled) {
    return 1000u;
  }
  const uint32_t period = static_cast<uint32_t>(60000.f / static_cast<float>(s_bpm));
  uint32_t interval = period / 56u;
  if (interval < MYNAH_HUE_GEM_PULSE_REPAINT_MIN_MS) {
    interval = MYNAH_HUE_GEM_PULSE_REPAINT_MIN_MS;
  }
  if (interval > 66u) {
    interval = 66u;
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
    Serial.println("gem: usage: gem pulse [on|off|bpm N]  (5-6-7 breath)");
    return true;
  }
  args += 5;
  while (*args == ' ') {
    ++args;
  }
  if (*args == '\0') {
    const float cycle_s = 60.f / static_cast<float>(s_bpm);
    Serial.printf("gem pulse: %s bpm=%u (%.1fs/cycle 5-6-7)\n", s_enabled ? "on" : "off",
                  static_cast<unsigned>(s_bpm), cycle_s);
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
