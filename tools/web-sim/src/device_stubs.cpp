#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "faces/pm_faces.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_battery_stats.h"
#include "pm_biometrics_model.h"
#include "pm_audio_analyzer.h"
#include "pm_birth_nvs.h"
#include "pm_calcifer.h"
#include "pm_castalia_auth.h"
#include "pm_chart_profiles.h"
#include "pm_colmi_r02.h"
#include "pm_commonplace.h"
#include "pm_display.h"
#include "pm_faculty.h"
#include "pm_heap.h"
#include "pm_motion.h"
#include "pm_power.h"
#include "pm_presence.h"
#include "pm_rtp_midi.h"
#include "pm_screen_http.h"
#include "pm_settings.h"
#include "pm_side_buttons.h"
#include "pm_speaker.h"
#include "pm_spotify.h"
#include "pm_touch.h"
#include "pm_transit.h"
#include "pm_usb_hid.h"
#include "pm_usb_midi.h"
#include "pm_variant.h"
#include "pm_weather.h"
#include "pm_wifi_ntp.h"

char g_gesture_banner[44] = {};

bool pm_time_valid(void) { return true; }
void pm_time_local(struct tm *out) {
  time_t now = 1716508800 + millis() / 1000;
  *out = *gmtime(&now);
}
void pm_time_utc(struct tm *out) { pm_time_local(out); }
int64_t pm_time_epoch(void) { return 1716508800 + millis() / 1000; }
bool pm_wifi_connected(void) { return false; }
void pm_wifi_pause_for_ble(void) {}
void pm_wifi_resume_after_ble(void) {}
const char *pm_wifi_mdns_name() { return "astrolabe-web-sim"; }
int32_t pm_geo_tz_offset_sec(void) { return 0; }

bool pm_variant_face_allowed(ClockFace) { return true; }
PmDeviceVariant pm_variant_get(void) { return PmDeviceVariant::Astrolabe; }
void pm_variant_set(PmDeviceVariant) {}
PmDeviceVariant pm_variant_cycle(int) { return PmDeviceVariant::Astrolabe; }
const char *pm_variant_label(PmDeviceVariant) { return "Astrolabe"; }
const char *pm_variant_summary(PmDeviceVariant) { return "web sim"; }
ClockFace pm_variant_home_face(void) { return ClockFace::ClassicAnalog; }
const char *pm_variant_device_platform(void) { return "web"; }
const char *pm_variant_ota_channel(void) { return "web-sim"; }

static SettingsPage s_settings_page = SettingsPage::WiFi;
void pm_settings_set_page(SettingsPage page) { s_settings_page = page; }
void pm_settings_cycle(int delta) {
  const int n = static_cast<int>(SettingsPage::kCount);
  s_settings_page = static_cast<SettingsPage>((static_cast<int>(s_settings_page) + delta + n) % n);
}
SettingsPage pm_settings_page(void) { return s_settings_page; }
const char *pm_settings_page_label(SettingsPage page) {
  switch (page) {
    case SettingsPage::WiFi: return "WiFi";
    case SettingsPage::Battery: return "Battery";
    case SettingsPage::Sleep: return "Sleep";
    case SettingsPage::Variant: return "Variant";
    case SettingsPage::Ota: return "OTA";
    case SettingsPage::Castalia: return "Castalia";
    case SettingsPage::kCount: break;
  }
  return "Settings";
}
void pm_settings_draw(void) {
  pm_gfx->fillScreen(pm_gfx->color565(8, 12, 18));
  pm_face_draw_centered_line("SETTINGS", 188, pm_gfx->color565(180, 195, 220), 2, 2);
  pm_face_draw_centered_line(pm_settings_page_label(s_settings_page), 224, pm_gfx->color565(120, 145, 180), 1, 1);
}

bool pm_castalia_draw_qr(PmDisplayCanvas *, int, int, int) { return false; }
const char *pm_castalia_signin_url_for_qr() { return ""; }
const char *pm_castalia_status_line() { return "web sim"; }
const char *pm_castalia_individual_id() { return "demo"; }
const char *pm_castalia_repo_name() { return "castalia-demo"; }
bool pm_castalia_has_session() { return false; }
void pm_castalia_note_qr_drawn(void) {}

uint32_t pm_heap_internal_free(void) { return 128u * 1024u; }
uint32_t pm_heap_internal_largest(void) { return 96u * 1024u; }
uint32_t pm_heap_psram_free(void) { return 0; }
bool pm_heap_tls_ready(uint32_t, const char *) { return true; }
void *pm_heap_alloc_response(size_t bytes) { return std::malloc(bytes); }
void pm_heap_log(const char *) {}

PmBatteryStats pm_battery_stats_update(const PmPmuStatus &, uint32_t) { return {}; }
PmPowerState pm_power_state(uint32_t) { return {}; }
void pm_power_cycle_dim_timeout(int) {}
void pm_power_cycle_sleep_timeout(int) {}
void pm_power_toggle_enabled(void) {}

bool pm_birth_load(PmBirthSpec *out) {
  if (out) {
    std::memset(out, 0, sizeof(*out));
    out->valid = true;
    out->year = 1990;
    out->month = 1;
    out->day = 1;
  }
  return out != nullptr;
}
bool pm_birth_to_utc_epoch(const PmBirthSpec *, time_t *utc_out) {
  if (utc_out) *utc_out = 631152000;
  return utc_out != nullptr;
}

void pm_transit_compute_utc(const struct tm *, PmTransitPositions *out) {
  if (!out) return;
  std::memset(out, 0, sizeof(*out));
  out->ok = true;
}
bool pm_transit_natal_sun_lon(const PmBirthSpec *, double *out) { if (out) *out = 280.0; return true; }
bool pm_transit_birth_positions(const PmBirthSpec *, PmTransitPositions *out) {
  pm_transit_compute_utc(nullptr, out);
  return out != nullptr;
}
void pm_ephemeris_release_cache(void) {}

void pm_chart_profiles_ensure_demo_seed(void) {}
int pm_chart_profile_count(void) { return 0; }
int pm_chart_profiles_active_slot(void) { return -1; }
bool pm_chart_profile_get(int, PmChartProfile *) { return false; }
bool pm_chart_profiles_active(PmChartProfile *) { return false; }
bool pm_chart_profile_to_birth(const PmChartProfile *, PmBirthSpec *) { return false; }
const char *pm_chart_role_label(PmChartRole) { return "profile"; }

void pm_audio_analyzer_get_mix(float *bands, size_t count) { for (size_t i = 0; i < count; ++i) bands[i] = 0.2f; }
void pm_audio_analyzer_get_waveform(float *samples, size_t count) {
  for (size_t i = 0; i < count; ++i) samples[i] = 0.0f;
}
void pm_audio_analyzer_get_spec_history(float *rows, int count, int bands) {
  if (!rows) return;
  for (int i = 0; i < count * bands; ++i) rows[i] = 0.1f;
}
void pm_audio_analyzer_get_pitch(PmAudioPitch *out) { if (out) std::memset(out, 0, sizeof(*out)); }
float pm_audio_analyzer_get_level(void) { return 0.2f; }
void pm_audio_analyzer_reset(void) {}
bool pm_audio_analyzer_mic_begin(void) { return true; }
void pm_audio_analyzer_mic_end(void) {}
void pm_audio_analyzer_tick(void) {}

bool pm_motion_has_6dof(void) { return true; }
void pm_motion_tick(uint32_t) {}
float pm_motion_yaw_deg(void) { return fmodf(millis() * 0.01f, 360.f); }
bool pm_motion_accel_norm(float *x, float *y, float *z) { if (x) *x = 0.1f; if (y) *y = -0.2f; if (z) *z = 0.98f; return true; }
bool pm_motion_accel_g(float *x, float *y, float *z) { return pm_motion_accel_norm(x, y, z); }

uint32_t pm_presence_self_id(void) { return 0xCA57; }
bool pm_presence_ble_is_ready(void) { return false; }
bool pm_presence_ble_failed(void) { return false; }
bool pm_presence_ble_begin(void) { return false; }
void pm_presence_ble_end(void) {}
void pm_presence_ble_set_radar_active(bool) {}
void pm_presence_seed_demo_peers(uint32_t) {}
void pm_presence_tick(uint32_t) {}
int pm_presence_count(void) { return 0; }
bool pm_presence_get(int, PmPresencePeer *) { return false; }
int pm_presence_rssi_ring(int8_t) { return 2; }

void pm_biometrics_model_tick(uint32_t) {}
void pm_biometrics_model_estimate(PmBiometricsEstimate *out) { if (out) std::memset(out, 0, sizeof(*out)); }
void pm_biometrics_model_format(const PmBiometricsEstimate *, char *out, size_t cap) {
  if (out && cap) std::snprintf(out, cap, "web sim estimate");
}

void pm_faculty_ensure_demo_seed(void) {}
void pm_faculty_ensure_seed(void) {}
void pm_faculty_prepare_demo_view(void) {}
int pm_faculty_count(void) { return 1; }
bool pm_faculty_active(PmFacultyProfile *out) {
  if (!out) return false;
  std::snprintf(out->slug, sizeof(out->slug), "hypatia");
  std::snprintf(out->name, sizeof(out->name), "Hypatia");
  return true;
}
bool pm_faculty_get_slot(int, PmFacultyProfile *out) { return pm_faculty_active(out); }
void pm_faculty_release_bust_cache(void) {}
static void websim_draw_faculty_bust(int cx, int bottom_y, int max_w, int max_h, const char *name) {
  if (!pm_gfx) return;

  const int head_r = max(14, min(max_w, max_h) / 7);
  const int head_cy = bottom_y - max_h + head_r * 3;
  const uint16_t robe = pm_gfx->color565(22, 44, 76);
  const uint16_t robe_hi = pm_gfx->color565(52, 88, 132);
  const uint16_t skin = pm_gfx->color565(203, 164, 125);
  const uint16_t skin_hi = pm_gfx->color565(236, 202, 165);
  const uint16_t hair = pm_gfx->color565(55, 42, 48);
  const uint16_t gold = pm_gfx->color565(214, 172, 85);
  const uint16_t line = pm_gfx->color565(10, 14, 24);
  const uint16_t text = pm_gfx->color565(218, 228, 240);

  pm_gfx->fillTriangle(cx - max_w / 3, bottom_y, cx + max_w / 3, bottom_y, cx, head_cy + head_r, robe);
  pm_gfx->fillTriangle(cx - max_w / 5, bottom_y, cx + max_w / 5, bottom_y, cx, head_cy + head_r * 2, robe_hi);
  pm_gfx->drawTriangle(cx - max_w / 3, bottom_y, cx + max_w / 3, bottom_y, cx, head_cy + head_r, gold);
  pm_gfx->fillCircle(cx, head_cy - head_r / 4, head_r + head_r / 4, hair);
  pm_gfx->fillCircle(cx, head_cy, head_r, skin);
  pm_gfx->fillCircle(cx - head_r / 3, head_cy - head_r / 4, max(2, head_r / 9), line);
  pm_gfx->fillCircle(cx + head_r / 3, head_cy - head_r / 4, max(2, head_r / 9), line);
  pm_gfx->drawFastHLine(cx - head_r / 3, head_cy + head_r / 3, (head_r * 2) / 3, line);
  pm_gfx->fillCircle(cx - head_r / 3, head_cy - head_r / 3, max(2, head_r / 8), skin_hi);
  pm_gfx->drawCircle(cx, head_cy, head_r, gold);
  pm_gfx->drawCircle(cx, head_cy, head_r + 5, pm_gfx->color565(62, 84, 116));

  for (int i = 0; i < 9; ++i) {
    const float a = -2.35f + i * (4.7f / 8.f);
    const int x0 = cx + static_cast<int>(cosf(a) * (head_r + 10));
    const int y0 = head_cy + static_cast<int>(sinf(a) * (head_r + 10));
    const int x1 = cx + static_cast<int>(cosf(a) * (head_r + 28));
    const int y1 = head_cy + static_cast<int>(sinf(a) * (head_r + 28));
    pm_gfx->drawLine(x0, y0, x1, y1, gold);
  }

  if (name && name[0] && max_w > 120) {
    pm_gfx->setTextColor(text);
    pm_gfx->setTextSize(2);
    int16_t x1, y1;
    uint16_t tw, th;
    pm_gfx->getTextBounds(name, 0, 0, &x1, &y1, &tw, &th);
    pm_gfx->setCursor(cx - static_cast<int>(tw) / 2, min(bottom_y - 22, LCD_HEIGHT - 28));
    pm_gfx->print(name);
  }
}
void pm_faculty_draw_bust(void) { websim_draw_faculty_bust(LCD_WIDTH / 2, LCD_HEIGHT - 34, LCD_WIDTH - 96, LCD_HEIGHT - 90, "Hypatia"); }
void pm_faculty_draw_bust_fullscreen(void) {
  pm_gfx->fillCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2, 210, pm_gfx->color565(10, 16, 30));
  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2, 214, pm_gfx->color565(74, 92, 126));
  websim_draw_faculty_bust(LCD_WIDTH / 2, LCD_HEIGHT - 44, LCD_WIDTH - 78, LCD_HEIGHT - 88, "Hypatia");
}
void pm_faculty_draw_bust_for(const PmFacultyProfile *profile) {
  websim_draw_faculty_bust(LCD_WIDTH / 2, LCD_HEIGHT - 34, LCD_WIDTH - 96, LCD_HEIGHT - 90,
                           profile && profile->name[0] ? profile->name : "Faculty");
}
void pm_faculty_draw_bust_for_at(const PmFacultyProfile *profile, int cx, int bottom_y, int max_w, int max_h, int, int) {
  websim_draw_faculty_bust(cx, bottom_y, max_w, max_h, profile && profile->name[0] ? profile->name : nullptr);
}
const char *pm_faculty_bust_slug(void) { return "hypatia"; }
bool pm_faculty_bust_ready_for(const char *) { return true; }

PmCommonplaceStatus pm_commonplace_status(void) { return PmCommonplaceStatus::Idle; }
size_t pm_commonplace_offline_note_count(void) { return 0; }
const char *pm_commonplace_last_transcript(void) { return ""; }
const char *pm_commonplace_last_error(void) { return ""; }

bool pm_speaker_is_playing(void) { return false; }
void pm_speaker_abort(void) {}
bool pm_speaker_play_tone_begin(float, uint32_t) { return false; }
bool pm_speaker_play_bongo_begin(float, float) { return false; }
bool pm_speaker_play_tone_loop_begin(float) { return false; }
void pm_speaker_tone_stop(void) {}
float pm_speaker_bowl_voice_energy(void) { return 0.0f; }
bool pm_speaker_bowl_voice_active(void) { return false; }
void pm_speaker_bowl_voice_stop(void) {}
void pm_speaker_bowl_voice_push(const PmBowlVoiceCtrl &) {}

uint8_t pm_touch_sample(int16_t *, int16_t *, uint8_t) { return 0; }

bool pm_colmi_r02_begin(void) { return false; }
void pm_colmi_r02_stop(void) {}
void pm_colmi_r02_tick(uint32_t) {}
bool pm_colmi_r02_ready(void) { return false; }
bool pm_colmi_r02_streaming(void) { return false; }
bool pm_colmi_r02_accel_g(PmColmiR02AccelSample *) { return false; }
const char *pm_colmi_r02_status_label(void) { return "ring offline"; }

bool pm_usb_hid_begin(void) { return false; }
bool pm_usb_hid_enabled(void) { return false; }
bool pm_usb_hid_ready(void) { return false; }
bool pm_usb_hid_mouse_move(int8_t, int8_t, int8_t, int8_t) { return false; }
bool pm_usb_hid_mouse_click(uint8_t) { return false; }
bool pm_usb_hid_gamepad_send(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t, uint32_t) { return false; }

bool pm_usb_midi_begin(void) { return false; }
bool pm_usb_midi_enabled(void) { return false; }
bool pm_usb_midi_note_on(uint8_t, uint8_t) { return false; }
bool pm_usb_midi_note_off(uint8_t) { return false; }
bool pm_rtp_midi_begin(void) { return false; }
void pm_rtp_midi_tick(void) {}
bool pm_rtp_midi_enabled(void) { return false; }
bool pm_rtp_midi_note_on(uint8_t, uint8_t) { return false; }
bool pm_rtp_midi_note_off(uint8_t) { return false; }

void pm_screen_http_begin(PmDisplayCanvas *) {}
void pm_screen_http_loop() {}
void pm_screen_http_ota_arm(uint32_t) {}
bool pm_screen_http_ota_armed(void) { return false; }
const char *pm_screen_http_ota_status(void) { return "web sim"; }
size_t pm_screen_http_ota_bytes(void) { return 0; }
size_t pm_screen_http_ota_total(void) { return 0; }
const char *pm_screen_http_ota_integration_url(void) { return ""; }

bool pm_calcifer_fetch(PmCalciferStatus *, time_t) { return false; }
bool pm_weather_fetch(PmWeatherStatus *) { return false; }
void pm_weather_fill_demo(PmWeatherStatus *out, int) {
  if (!out) return;
  out->ok = true;
  out->demo = true;
  out->current_temp_c = 22;
  out->hi_c = 26;
  out->lo_c = 16;
  std::snprintf(out->condition, sizeof(out->condition), "clear");
  std::snprintf(out->location, sizeof(out->location), "web sim");
  for (int i = 0; i < 24; ++i) {
    out->hourly[i].temp_c = static_cast<int8_t>(18 + (i % 8));
    out->hourly[i].humidity_pct = static_cast<uint8_t>(45 + (i % 20));
  }
}
void pm_rocket_pad_image_release(void) {}
bool pm_rocket_pad_image_ready(void) { return false; }
void pm_rocket_pad_image_draw_background(int, int, int, uint16_t, float) {}

float pm_home_gem_pulse_breath_amount(uint32_t now_ms) {
  return 0.5f + 0.5f * sinf(static_cast<float>(now_ms) * 0.004f);
}
