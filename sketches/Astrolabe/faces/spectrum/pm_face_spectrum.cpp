#include "faces/spectrum/pm_face_spectrum.h"

#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_audio_analyzer.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

enum class VizMode : uint8_t {
  Mandala = 0,
  SpectrumRing,
  Oscilloscope,
  Spectrogram,
  Petals,
  Lissajous,
  kCount,
};

static bool s_active = false;
static int s_mode = 0;
static float s_hue_spin = 0.f;

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kR = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2 - 14;

static constexpr int kR_spec_outer = kR - 6;
static constexpr int kR_spec_inner = kR - 44;
static constexpr int kR_hist_outer = kR - 48;
static constexpr int kR_hist_inner = kR - 108;
static constexpr int kR_wave = kR - 118;
static constexpr int kR_wave_amp = 28;
static constexpr int kR_petal_base = 52;
static constexpr int kR_center = 16;

static const char *mode_label(int mode) {
  switch (static_cast<VizMode>(mode)) {
    case VizMode::Mandala:
      return "mandala";
    case VizMode::SpectrumRing:
      return "spectrum";
    case VizMode::Oscilloscope:
      return "scope";
    case VizMode::Spectrogram:
      return "spectrogram";
    case VizMode::Petals:
      return "petals";
    case VizMode::Lissajous:
      return "lissajous";
    default:
      return "viz";
  }
}

static float clock_hue_deg(void) {
  struct tm tm = {};
  if (pm_time_valid()) {
    pm_time_local(&tm);
    const int sec = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return static_cast<float>(sec) * (360.f / 86400.f);
  }
  return fmodf(static_cast<float>(millis()) * 0.02f, 360.f);
}

static void draw_background(void) {
  pm_gfx->fillScreen(pm_gfx->color565(4, 6, 14));
  const uint16_t ring = pm_gfx->color565(18, 22, 36);
  for (int r = kR; r >= kR_center; r -= 18) {
    pm_gfx->drawCircle(kCx, kCy, r, ring);
  }
}

static void draw_mode_caption(const char *name, int index, int total) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %d/%d", name, index + 1, total);
  pm_face_draw_centered_line(buf, 24, pm_gfx->color565(120, 130, 160), 1, 1);
}

static void draw_time_context_ring(float hue_base) {
  const float hue = hue_base + s_hue_spin;
  const uint16_t tick = pm_face_color565_from_hsv(pm_gfx, hue, 0.35f, 0.22f);
  const float a_now = pm_face_deg_to_rad(clock_hue_deg());
  const int bx = kCx + static_cast<int>(cosf(a_now) * static_cast<float>(kR - 2));
  const int by = kCy + static_cast<int>(sinf(a_now) * static_cast<float>(kR - 2));
  pm_gfx->fillCircle(bx, by, 5, tick);
  pm_gfx->drawCircle(kCx, kCy, kR, pm_gfx->color565(28, 32, 48));
}

static void draw_spec_history_rings(const float *hist_rows, int nrows, int nbands, float hue_base, int r_outer,
                                  int r_inner) {
  if (!hist_rows || nrows <= 0 || nbands <= 0 || r_outer <= r_inner) {
    return;
  }
  const float dr = static_cast<float>(r_outer - r_inner) / static_cast<float>(nrows > 1 ? nrows - 1 : 1);
  for (int row = 0; row < nrows; ++row) {
    const float t_row = static_cast<float>(row) / static_cast<float>(nrows > 1 ? nrows - 1 : 1);
    const int r0 = r_outer - static_cast<int>(t_row * dr);
    const int r1 = r0 - static_cast<int>(dr * 0.85f);
    if (r1 < r_inner) {
      continue;
    }
    const float *bands = hist_rows + row * nbands;
    const float fade = 0.12f + 0.22f * t_row;
    for (int b = 0; b < nbands; ++b) {
      const float v = bands[b];
      if (v < 0.04f) {
        continue;
      }
      const float a0 = pm_face_deg_to_rad(static_cast<float>(b) * (360.f / static_cast<float>(nbands)));
      const float a1 = pm_face_deg_to_rad(static_cast<float>(b + 1) * (360.f / static_cast<float>(nbands)));
      const float span = a1 - a0;
      for (int s = 0; s < 3; ++s) {
        const float u = (static_cast<float>(s) + 0.5f) / 3.f;
        const float ang = a0 + span * u;
        const uint16_t col =
            pm_face_color565_from_hsv(pm_gfx, hue_base + static_cast<float>(b) * 4.f + s_hue_spin, 0.7f,
                                      fade * (0.25f + v * 0.75f));
        pm_face_draw_radial_annulus_slice(kCx, kCy, ang, r1, r0, col, v > 0.35f ? 2 : 1);
      }
    }
  }
}

static void draw_radial_spectrum_ring(const float *bands, int n_bands, float hue_base, int r_outer, int r_inner) {
  if (!bands || n_bands <= 0 || r_outer <= r_inner) {
    return;
  }
  constexpr int k_slices = 48;
  for (int i = 0; i < k_slices; ++i) {
    const int bi = (i * n_bands) / k_slices;
    const float v = bands[bi < n_bands ? bi : n_bands - 1];
    if (v < 0.03f) {
      continue;
    }
    const float a = pm_face_deg_to_rad(static_cast<float>(i) * (360.f / static_cast<float>(k_slices)));
    const int extent = static_cast<int>(v * static_cast<float>(r_outer - r_inner));
    const int r0 = r_inner;
    const int r1 = r_inner + (extent < 4 ? 4 : extent);
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, hue_base + static_cast<float>(i) * 2.8f + s_hue_spin,
                                                   0.82f, 0.18f + v * 0.72f);
    pm_face_draw_radial_annulus_slice(kCx, kCy, a, r0, r1, col, v > 0.5f ? 3 : 2);
  }
}

static void draw_circular_oscilloscope(const float *wave, int n, float hue_base, int r_base, int r_amp) {
  if (!wave || n < 4 || r_amp < 4) {
    return;
  }
  int px0 = 0;
  int py0 = 0;
  bool have0 = false;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, hue_base + 40.f + s_hue_spin, 0.65f, 0.55f);
  for (int i = 0; i <= n; ++i) {
    const int ii = i < n ? i : 0;
    const float a = pm_face_deg_to_rad(static_cast<float>(ii) * (360.f / static_cast<float>(n)));
    const float v = wave[ii];
    const int r = r_base + static_cast<int>(v * static_cast<float>(r_amp));
    const int px = kCx + static_cast<int>(cosf(a) * static_cast<float>(r));
    const int py = kCy + static_cast<int>(sinf(a) * static_cast<float>(r));
    if (have0) {
      pm_gfx->drawLine(px0, py0, px, py, col);
    }
    px0 = px;
    py0 = py;
    have0 = true;
  }
}

static void draw_petal_core(const float *bands, int n_bands, float level, float hue_base, int petal_base) {
  constexpr int k_petals = 7;
  float band_energy[k_petals];
  for (int p = 0; p < k_petals; ++p) {
    band_energy[p] = 0.f;
  }
  for (int b = 0; b < n_bands; ++b) {
    const int p = (b * k_petals) / n_bands;
    if (bands[b] > band_energy[p]) {
      band_energy[p] = bands[b];
    }
  }
  const uint16_t soft = pm_face_color565_from_hsv(pm_gfx, hue_base + s_hue_spin, 0.5f, 0.12f + level * 0.2f);
  pm_gfx->fillCircle(kCx, kCy, petal_base + 8, soft);

  for (int p = 0; p < k_petals; ++p) {
    const float a = pm_face_deg_to_rad(static_cast<float>(p) * (360.f / static_cast<float>(k_petals)));
    const float v = band_energy[p];
    const int reach = petal_base + static_cast<int>(v * 48.f);
    const int tip_x = kCx + static_cast<int>(cosf(a) * static_cast<float>(reach));
    const int tip_y = kCy + static_cast<int>(sinf(a) * static_cast<float>(reach));
    const int base_r = 12 + static_cast<int>(v * 10.f);
    const float px = -sinf(a);
    const float py = cosf(a);
    const int lx = kCx + static_cast<int>(px * static_cast<float>(base_r));
    const int ly = kCy + static_cast<int>(py * static_cast<float>(base_r));
    const int rx = kCx - static_cast<int>(px * static_cast<float>(base_r));
    const int ry = kCy - static_cast<int>(py * static_cast<float>(base_r));
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, hue_base + static_cast<float>(p) * 18.f + s_hue_spin,
                                                   0.78f, 0.22f + v * 0.65f);
    pm_gfx->fillTriangle(lx, ly, rx, ry, tip_x, tip_y, col);
  }

  const uint16_t core = pm_face_color565_from_hsv(pm_gfx, hue_base + 120.f + s_hue_spin, 0.4f, 0.08f + level * 0.35f);
  pm_gfx->fillCircle(kCx, kCy, kR_center + static_cast<int>(level * 10.f), core);
}

static void draw_lissajous(const float *wave, int n, float hue_base) {
  if (!wave || n < 8) {
    return;
  }
  const int span = kR - 36;
  const int phase = n / 4;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, hue_base + 60.f + s_hue_spin, 0.72f, 0.62f);
  int px0 = 0;
  int py0 = 0;
  bool have0 = false;
  for (int i = 0; i <= n; ++i) {
    const int ii = i < n ? i : 0;
    const int jj = (ii + phase) % n;
    const int px = kCx + static_cast<int>(wave[ii] * static_cast<float>(span));
    const int py = kCy + static_cast<int>(wave[jj] * static_cast<float>(span));
    if (have0) {
      pm_gfx->drawLine(px0, py0, px, py, col);
    }
    px0 = px;
    py0 = py;
    have0 = true;
  }
  pm_gfx->drawCircle(kCx, kCy, span, pm_gfx->color565(32, 36, 52));
}

void pm_face_spectrum_on_enter(void) {
  pm_audio_analyzer_reset();
  (void)pm_audio_analyzer_mic_begin();
  s_active = true;
  s_mode = 0;
  s_hue_spin = 0.f;
}

void pm_face_spectrum_on_leave(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_mic_end();
  s_active = false;
}

void pm_face_spectrum_cycle(int delta) {
  const int n = static_cast<int>(VizMode::kCount);
  int v = s_mode + delta;
  v = (v % n + n) % n;
  s_mode = v;
}

int pm_face_spectrum_mode(void) { return s_mode; }

int pm_face_spectrum_mode_count(void) { return static_cast<int>(VizMode::kCount); }

const char *pm_face_spectrum_mode_label(void) { return mode_label(s_mode); }

void pm_face_spectrum_tick(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_tick();
  s_hue_spin += 0.35f;
  if (s_hue_spin >= 360.f) {
    s_hue_spin -= 360.f;
  }
}

void pm_face_spectrum_draw(uint16_t bg) {
  (void)bg;

  float mix[PM_AUDIO_ANALYZER_BANDS];
  float wave[PM_AUDIO_WAVE_POINTS];
  float hist[PM_AUDIO_SPEC_HISTORY * PM_AUDIO_ANALYZER_BANDS];
  pm_audio_analyzer_get_mix(mix, PM_AUDIO_ANALYZER_BANDS);
  pm_audio_analyzer_get_waveform(wave, PM_AUDIO_WAVE_POINTS);
  pm_audio_analyzer_get_spec_history(hist, PM_AUDIO_SPEC_HISTORY, PM_AUDIO_ANALYZER_BANDS);
  const float level = pm_audio_analyzer_get_level();
  const float hue_base = clock_hue_deg();
  const int mode_count = static_cast<int>(VizMode::kCount);

  draw_background();

  switch (static_cast<VizMode>(s_mode)) {
    case VizMode::Mandala:
      draw_time_context_ring(hue_base);
      draw_spec_history_rings(hist, PM_AUDIO_SPEC_HISTORY, PM_AUDIO_ANALYZER_BANDS, hue_base + 200.f,
                              kR_hist_outer, kR_hist_inner);
      draw_radial_spectrum_ring(mix, PM_AUDIO_ANALYZER_BANDS, hue_base, kR_spec_outer, kR_spec_inner);
      draw_circular_oscilloscope(wave, PM_AUDIO_WAVE_POINTS, hue_base, kR_wave, kR_wave_amp);
      draw_petal_core(mix, PM_AUDIO_ANALYZER_BANDS, level, hue_base, kR_petal_base);
      break;
    case VizMode::SpectrumRing:
      draw_radial_spectrum_ring(mix, PM_AUDIO_ANALYZER_BANDS, hue_base, kR - 8, kR_center + 8);
      break;
    case VizMode::Oscilloscope:
      draw_circular_oscilloscope(wave, PM_AUDIO_WAVE_POINTS, hue_base, kR - 58, 46);
      break;
    case VizMode::Spectrogram:
      draw_spec_history_rings(hist, PM_AUDIO_SPEC_HISTORY, PM_AUDIO_ANALYZER_BANDS, hue_base + 180.f, kR - 10,
                              kR_center);
      break;
    case VizMode::Petals:
      draw_petal_core(mix, PM_AUDIO_ANALYZER_BANDS, level, hue_base, kR - 70);
      break;
    case VizMode::Lissajous:
      draw_lissajous(wave, PM_AUDIO_WAVE_POINTS, hue_base);
      break;
    default:
      break;
  }

  draw_mode_caption(mode_label(s_mode), s_mode, mode_count);
}
