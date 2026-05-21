#include "faces/spectrum/pm_face_spectrum_viz.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pm_audio_analyzer.h"
#include "pm_display.h"
#include "pm_home_gem_pulse.h"
#include "pm_wifi_ntp.h"

namespace {

enum class VizId : uint8_t {
  Mandala = 0,
  SpectrumRing,
  Oscilloscope,
  Spectrogram,
  Petals,
  Lissajous,
  BreathOrb,
  BreathHalo,
  Rose,
  Spirograph,
  Phyllotaxis,
  Plasma,
  Firefly,
  PulseRipple,
  Starfield,
  Kaleidoscope,
  Cellular,
  kCount = PM_SPECTRUM_VIZ_COUNT,
};

static const char *const k_labels[] = {
    "mandala",    "spectrum",  "scope",     "spectrogram", "petals",   "lissajous",
    "breath",     "halo",      "rose",      "spiro",       "phyllota", "plasma",
    "firefly",    "ripple",    "stars",     "kaleido",     "cellular",
};

static constexpr int kR_center = 16;

struct Firefly {
  float x;
  float y;
  float vx;
  float vy;
  float phase;
};

static Firefly s_flies[22];
static bool s_flies_init = false;
static float s_ripples[6];
static uint8_t s_cells[72];
static bool s_cells_init = false;
static float s_phy_bright[140];

static void draw_background(int cx, int cy, int R) {
  pm_gfx->fillScreen(pm_gfx->color565(4, 6, 14));
  const uint16_t ring = pm_gfx->color565(18, 22, 36);
  for (int r = R; r >= kR_center; r -= 18) {
    pm_gfx->drawCircle(cx, cy, r, ring);
  }
}

static void draw_time_bead(const PmSpectrumVizCtx &c, float clock_hue) {
  const float a = pm_face_deg_to_rad(clock_hue);
  const int bx = c.cx + static_cast<int>(cosf(a) * static_cast<float>(c.R - 2));
  const int by = c.cy + static_cast<int>(sinf(a) * static_cast<float>(c.R - 2));
  const uint16_t tick = pm_face_color565_from_hsv(pm_gfx, c.hue_base + c.hue_spin, 0.35f, 0.22f);
  pm_gfx->fillCircle(bx, by, 5, tick);
  pm_gfx->drawCircle(c.cx, c.cy, c.R, pm_gfx->color565(28, 32, 48));
}

static void draw_spec_history(const PmSpectrumVizCtx &c, float hue_off, int r_outer, int r_inner) {
  if (!c.hist || c.n_hist_rows <= 0 || c.n_bands <= 0 || r_outer <= r_inner) {
    return;
  }
  const float dr = static_cast<float>(r_outer - r_inner) / static_cast<float>(c.n_hist_rows > 1 ? c.n_hist_rows - 1 : 1);
  for (int row = 0; row < c.n_hist_rows; ++row) {
    const float t_row = static_cast<float>(row) / static_cast<float>(c.n_hist_rows > 1 ? c.n_hist_rows - 1 : 1);
    const int r0 = r_outer - static_cast<int>(t_row * dr);
    const int r1 = r0 - static_cast<int>(dr * 0.85f);
    if (r1 < r_inner) {
      continue;
    }
    const float *bands = c.hist + row * c.n_bands;
    const float fade = 0.12f + 0.22f * t_row;
    for (int b = 0; b < c.n_bands; ++b) {
      const float v = bands[b];
      if (v < 0.04f) {
        continue;
      }
      const float a0 = pm_face_deg_to_rad(static_cast<float>(b) * (360.f / static_cast<float>(c.n_bands)));
      const float a1 = pm_face_deg_to_rad(static_cast<float>(b + 1) * (360.f / static_cast<float>(c.n_bands)));
      const float ang = a0 + (a1 - a0) * 0.5f;
      const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + hue_off + static_cast<float>(b) * 4.f + c.hue_spin,
                                                     0.7f, fade * (0.25f + v * 0.75f));
      pm_face_draw_radial_annulus_slice(c.cx, c.cy, ang, r1, r0, col, v > 0.35f ? 2 : 1);
    }
  }
}

static void draw_spectrum_ring(const PmSpectrumVizCtx &c, int r_outer, int r_inner) {
  if (!c.mix || c.n_bands <= 0) {
    return;
  }
  constexpr int k_slices = 48;
  for (int i = 0; i < k_slices; ++i) {
    const int bi = (i * c.n_bands) / k_slices;
    const float v = c.mix[bi < c.n_bands ? bi : c.n_bands - 1];
    if (v < 0.03f) {
      continue;
    }
    const float a = pm_face_deg_to_rad(static_cast<float>(i) * (360.f / static_cast<float>(k_slices)));
    const int extent = static_cast<int>(v * static_cast<float>(r_outer - r_inner));
    const int r0 = r_inner;
    const int r1 = r_inner + (extent < 4 ? 4 : extent);
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + static_cast<float>(i) * 2.8f + c.hue_spin,
                                                   0.82f, 0.18f + v * 0.72f);
    pm_face_draw_radial_annulus_slice(c.cx, c.cy, a, r0, r1, col, v > 0.5f ? 3 : 2);
  }
}

static void draw_scope_ring(const PmSpectrumVizCtx &c, int r_base, int r_amp) {
  if (!c.wave || c.n_wave < 4 || r_amp < 4) {
    return;
  }
  int px0 = 0;
  int py0 = 0;
  bool have0 = false;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + 40.f + c.hue_spin, 0.65f, 0.55f);
  for (int i = 0; i <= c.n_wave; ++i) {
    const int ii = i < c.n_wave ? i : 0;
    const float a = pm_face_deg_to_rad(static_cast<float>(ii) * (360.f / static_cast<float>(c.n_wave)));
    const int r = r_base + static_cast<int>(c.wave[ii] * static_cast<float>(r_amp));
    const int px = c.cx + static_cast<int>(cosf(a) * static_cast<float>(r));
    const int py = c.cy + static_cast<int>(sinf(a) * static_cast<float>(r));
    if (have0) {
      pm_gfx->drawLine(px0, py0, px, py, col);
    }
    px0 = px;
    py0 = py;
    have0 = true;
  }
}

static void draw_petals(const PmSpectrumVizCtx &c, int petal_base) {
  if (!c.mix || c.n_bands <= 0) {
    return;
  }
  constexpr int k_petals = 7;
  float band_energy[k_petals];
  for (int p = 0; p < k_petals; ++p) {
    band_energy[p] = 0.f;
  }
  for (int b = 0; b < c.n_bands; ++b) {
    const int p = (b * k_petals) / c.n_bands;
    if (c.mix[b] > band_energy[p]) {
      band_energy[p] = c.mix[b];
    }
  }
  const uint16_t soft = pm_face_color565_from_hsv(pm_gfx, c.hue_base + c.hue_spin, 0.5f, 0.12f + c.level * 0.2f);
  pm_gfx->fillCircle(c.cx, c.cy, petal_base + 8, soft);
  for (int p = 0; p < k_petals; ++p) {
    const float a = pm_face_deg_to_rad(static_cast<float>(p) * (360.f / static_cast<float>(k_petals)));
    const float v = band_energy[p];
    const int reach = petal_base + static_cast<int>(v * 48.f);
    const int tip_x = c.cx + static_cast<int>(cosf(a) * static_cast<float>(reach));
    const int tip_y = c.cy + static_cast<int>(sinf(a) * static_cast<float>(reach));
    const int base_r = 12 + static_cast<int>(v * 10.f);
    const float px = -sinf(a);
    const float py = cosf(a);
    const int lx = c.cx + static_cast<int>(px * static_cast<float>(base_r));
    const int ly = c.cy + static_cast<int>(py * static_cast<float>(base_r));
    const int rx = c.cx - static_cast<int>(px * static_cast<float>(base_r));
    const int ry = c.cy - static_cast<int>(py * static_cast<float>(base_r));
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + static_cast<float>(p) * 18.f + c.hue_spin,
                                                   0.78f, 0.22f + v * 0.65f);
    pm_gfx->fillTriangle(lx, ly, rx, ry, tip_x, tip_y, col);
  }
  const uint16_t core =
      pm_face_color565_from_hsv(pm_gfx, c.hue_base + 120.f + c.hue_spin, 0.4f, 0.08f + c.level * 0.35f);
  pm_gfx->fillCircle(c.cx, c.cy, kR_center + static_cast<int>(c.level * 10.f), core);
}

static void draw_lissajous(const PmSpectrumVizCtx &c) {
  if (!c.wave || c.n_wave < 8) {
    return;
  }
  const int span = c.R - 36;
  const int phase = c.n_wave / 4;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + 60.f + c.hue_spin, 0.72f, 0.62f);
  int px0 = 0;
  int py0 = 0;
  bool have0 = false;
  for (int i = 0; i <= c.n_wave; ++i) {
    const int ii = i < c.n_wave ? i : 0;
    const int jj = (ii + phase) % c.n_wave;
    const int px = c.cx + static_cast<int>(c.wave[ii] * static_cast<float>(span));
    const int py = c.cy + static_cast<int>(c.wave[jj] * static_cast<float>(span));
    if (have0) {
      pm_gfx->drawLine(px0, py0, px, py, col);
    }
    px0 = px;
    py0 = py;
    have0 = true;
  }
  pm_gfx->drawCircle(c.cx, c.cy, span, pm_gfx->color565(32, 36, 52));
}

static void draw_breath_orb(const PmSpectrumVizCtx &c) {
  const float breath = pm_home_gem_pulse_breath_amount(millis());
  const float pulse = c.level * c.level;
  const float mod = 0.5f + 0.5f * pulse;
  const int r = static_cast<int>((static_cast<float>(c.R) * 0.20f + breath * static_cast<float>(c.R) * 0.30f) *
                                 mod + pulse * 30.f);
  const uint16_t inner =
      pm_face_color565_from_hsv(pm_gfx, c.hue_base + 40.f + c.hue_spin, 0.55f, 0.28f + breath * 0.25f + pulse * 0.35f);
  const uint16_t outer = pm_face_color565_from_hsv(pm_gfx, c.hue_base + c.hue_spin, 0.45f, 0.08f + pulse * 0.18f);
  pm_gfx->fillCircle(c.cx, c.cy, r + 14, outer);
  pm_gfx->fillCircle(c.cx, c.cy, r, inner);
}

static void draw_breath_halo(const PmSpectrumVizCtx &c) {
  const float breath = pm_home_gem_pulse_breath_amount(millis());
  const float start = pm_face_deg_to_rad(-90.f);
  const float sweep = breath * (0.55f + c.level * 0.45f);
  const float end = start + pm_face_k_two_pi * sweep;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + 80.f + c.hue_spin, 0.7f, 0.25f + c.level * 0.45f);
  const int r0 = c.R - 30 - static_cast<int>(c.level * 18.f);
  const int r1 = c.R - 8;
  constexpr int k_steps = 72;
  for (int i = 0; i < k_steps; ++i) {
    const float t0 = static_cast<float>(i) / static_cast<float>(k_steps);
    const float t1 = static_cast<float>(i + 1) / static_cast<float>(k_steps);
    if (t1 > sweep) {
      break;
    }
    const float a0 = start + (end - start) * t0;
    const float a1 = start + (end - start) * t1;
    pm_face_draw_radial_annulus_slice(c.cx, c.cy, a0 + (a1 - a0) * 0.5f, r0, r1, col, 3);
  }
  pm_gfx->drawCircle(c.cx, c.cy, r1, pm_gfx->color565(40, 44, 60));
}

static void draw_rose(const PmSpectrumVizCtx &c) {
  const float k = 7.f + c.level * 4.f;
  const int px0 = c.cx;
  const int py0 = c.cy;
  int lx = px0;
  int ly = py0;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + c.hue_spin, 0.75f, 0.5f);
  constexpr int k_steps = 240;
  for (int i = 0; i <= k_steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(k_steps) * pm_face_k_two_pi;
    const float r = cosf(k * t) * static_cast<float>(c.R - 30) * (0.35f + 0.65f * c.level);
    const int x = c.cx + static_cast<int>(r * cosf(t));
    const int y = c.cy + static_cast<int>(r * sinf(t));
    if (i > 0) {
      pm_gfx->drawLine(lx, ly, x, y, col);
    }
    lx = x;
    ly = y;
  }
}

static void draw_spirograph(const PmSpectrumVizCtx &c) {
  const float R = static_cast<float>(c.R - 40);
  const float r = 18.f + c.level * 12.f;
  const float d = 28.f + c.level * 40.f;
  const float spin = static_cast<float>(millis()) * 0.0004f;
  int lx = c.cx;
  int ly = c.cy;
  const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + 100.f + c.hue_spin, 0.7f, 0.55f);
  constexpr int k_steps = 280;
  for (int i = 0; i <= k_steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(k_steps) * pm_face_k_two_pi * 3.f + spin;
    const float x = (R - r) * cosf(t) + d * cosf((R - r) / r * t);
    const float y = (R - r) * sinf(t) - d * sinf((R - r) / r * t);
    const int px = c.cx + static_cast<int>(x);
    const int py = c.cy + static_cast<int>(y);
    if (i > 0) {
      pm_gfx->drawLine(lx, ly, px, py, col);
    }
    lx = px;
    ly = py;
  }
}

static void draw_phyllotaxis(const PmSpectrumVizCtx &c) {
  constexpr int k_n = 120;
  constexpr float k_golden = 2.39996323f;
  const float scale = static_cast<float>(c.R - 50) / sqrtf(static_cast<float>(k_n));
  for (int i = 0; i < k_n; ++i) {
    const float r = scale * sqrtf(static_cast<float>(i));
    const float a = static_cast<float>(i) * k_golden + c.hue_spin * 0.02f;
    const int x = c.cx + static_cast<int>(r * cosf(a));
    const int y = c.cy + static_cast<int>(r * sinf(a));
    const float b = s_phy_bright[i];
    if (b < 0.05f) {
      continue;
    }
    const int rad = 2 + static_cast<int>(b * 4.f);
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + static_cast<float>(i) * 1.4f, 0.65f, 0.15f + b * 0.6f);
    pm_gfx->fillCircle(x, y, rad, col);
  }
}

static void draw_plasma(const PmSpectrumVizCtx &c) {
  const float t = static_cast<float>(millis()) * 0.002f;
  const int step = 14;
  for (int y = c.cy - c.R; y <= c.cy + c.R; y += step) {
    for (int x = c.cx - c.R; x <= c.cx + c.R; x += step) {
      const int dx = x - c.cx;
      const int dy = y - c.cy;
      if (dx * dx + dy * dy > c.R * c.R) {
        continue;
      }
      const float v = 0.5f + 0.5f * sinf(static_cast<float>(x) * 0.04f + t) +
                      0.5f * sinf(static_cast<float>(y) * 0.05f - t * 1.2f) + c.level * 0.4f;
      const float vv = v > 1.f ? 1.f : (v < 0.f ? 0.f : v);
      const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + vv * 120.f + c.hue_spin, 0.85f, 0.12f + vv * 0.5f);
      pm_gfx->fillRect(x, y, step - 1, step - 1, col);
    }
  }
}

static void draw_fireflies(const PmSpectrumVizCtx &c) {
  for (int i = 0; i < static_cast<int>(sizeof(s_flies) / sizeof(s_flies[0])); ++i) {
    Firefly &f = s_flies[i];
    const float blink = 0.5f + 0.5f * sinf(f.phase);
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + 140.f + static_cast<float>(i) * 7.f, 0.5f,
                                                   0.06f + blink * 0.25f + c.level * 0.42f);
    pm_gfx->fillCircle(static_cast<int>(f.x), static_cast<int>(f.y),
                       2 + static_cast<int>(blink * 2.f) + static_cast<int>(c.level * 4.f), col);
  }
}

static void draw_ripples(const PmSpectrumVizCtx &c) {
  for (int i = 0; i < static_cast<int>(sizeof(s_ripples) / sizeof(s_ripples[0])); ++i) {
    const float r = s_ripples[i];
    if (r < 4.f) {
      continue;
    }
    const float fade = 1.f - r / static_cast<float>(c.R);
    if (fade <= 0.f) {
      continue;
    }
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, c.hue_base + 200.f + c.hue_spin, 0.5f, fade * 0.35f);
    pm_gfx->drawCircle(c.cx, c.cy, static_cast<int>(r), col);
  }
}

static void draw_starfield(const PmSpectrumVizCtx &c) {
  const float rot = static_cast<float>(millis()) * (0.00018f + c.level * 0.0011f);
  constexpr int k_stars = 48;
  int sx[k_stars];
  int sy[k_stars];
  for (int i = 0; i < k_stars; ++i) {
    const float a = static_cast<float>(i) * (pm_face_k_two_pi / static_cast<float>(k_stars)) + rot;
    const int r = 24 + (i * 17) % (c.R - 46) + static_cast<int>(c.level * 18.f);
    sx[i] = c.cx + static_cast<int>(cosf(a) * static_cast<float>(r));
    sy[i] = c.cy + static_cast<int>(sinf(a) * static_cast<float>(r));
    const uint16_t col =
        pm_face_color565_from_hsv(pm_gfx, c.hue_base + static_cast<float>(i) * 5.f, 0.4f, 0.18f + c.level * 0.55f);
    pm_gfx->fillCircle(sx[i], sy[i], 1 + static_cast<int>(c.level * 4.f), col);
  }
  for (int i = 0; i < k_stars; ++i) {
    const int j = (i * 7 + 3) % k_stars;
    if ((i + j) % (c.level > 0.45f ? 3 : 5) != 0) {
      continue;
    }
    const uint8_t line = static_cast<uint8_t>(45 + c.level * 70.f);
    pm_gfx->drawLine(sx[i], sy[i], sx[j], sy[j], pm_gfx->color565(line, line + 8, line + 28));
  }
}

static float kaleido_sample(float x, float y, float t) {
  return 0.5f + 0.5f * sinf(x * 0.12f + t) + 0.5f * sinf(y * 0.15f - t * 0.8f);
}

static void draw_kaleidoscope(const PmSpectrumVizCtx &c) {
  const float t = static_cast<float>(millis()) * (0.002f + c.level * 0.006f) + c.level * 4.f;
  const int patch = 36;
  for (int py = 0; py < patch; ++py) {
    for (int px = 0; px < patch; ++px) {
      const float v = kaleido_sample(static_cast<float>(px), static_cast<float>(py), t);
      const uint16_t col =
          pm_face_color565_from_hsv(pm_gfx, c.hue_base + v * 90.f + c.hue_spin, 0.8f, 0.12f + v * 0.35f + c.level * 0.28f);
      for (int seg = 0; seg < 6; ++seg) {
        const float ang = static_cast<float>(seg) * (pm_face_k_two_pi / 6.f);
        const float ca = cosf(ang);
        const float sa = sinf(ang);
        const int x = c.cx + static_cast<int>((static_cast<float>(px) * ca - static_cast<float>(py) * sa));
        const int y = c.cy + static_cast<int>((static_cast<float>(px) * sa + static_cast<float>(py) * ca));
        pm_gfx->drawPixel(x, y, col);
      }
    }
  }
}

static void draw_cellular_ring(const PmSpectrumVizCtx &c) {
  const int n = static_cast<int>(sizeof(s_cells) / sizeof(s_cells[0]));
  const int r0 = c.R - 70;
  const int r1 = c.R - 20;
  for (int i = 0; i < n; ++i) {
    const float a = static_cast<float>(i) * (pm_face_k_two_pi / static_cast<float>(n)) - pm_face_k_pi * 0.5f;
    const float v = static_cast<float>(s_cells[i]) / 255.f;
    const uint16_t col =
        pm_face_color565_from_hsv(pm_gfx, c.hue_base + v * 100.f + c.hue_spin, 0.75f, 0.10f + v * 0.42f + c.level * 0.35f);
    pm_face_draw_radial_annulus_slice(c.cx, c.cy, a, r0 - static_cast<int>(c.level * 18.f), r1, col,
                                      (s_cells[i] > 128 || c.level > 0.55f) ? 3 : 2);
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

}  // namespace

void pm_face_spectrum_viz_reset(void) {
  s_flies_init = false;
  s_cells_init = false;
  memset(s_ripples, 0, sizeof(s_ripples));
  memset(s_phy_bright, 0, sizeof(s_phy_bright));
}

void pm_face_spectrum_viz_tick(float level) {
  if (!s_flies_init) {
    for (size_t i = 0; i < sizeof(s_flies) / sizeof(s_flies[0]); ++i) {
      s_flies[i].x = static_cast<float>(pm_face_lcd_cx) + static_cast<float>((i * 37) % 200) - 100.f;
      s_flies[i].y = static_cast<float>(pm_face_lcd_cy) + static_cast<float>((i * 53) % 200) - 100.f;
      s_flies[i].vx = static_cast<float>((i % 5) - 2) * 0.35f;
      s_flies[i].vy = static_cast<float>((i % 7) - 3) * 0.28f;
      s_flies[i].phase = static_cast<float>(i);
    }
    s_flies_init = true;
  }
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int R = 220;
  for (size_t i = 0; i < sizeof(s_flies) / sizeof(s_flies[0]); ++i) {
    Firefly &f = s_flies[i];
    const float speed = 1.f + level * 2.6f;
    f.x += f.vx * speed;
    f.y += f.vy * speed;
    f.phase += 0.08f + level * 0.28f;
    const int dx = static_cast<int>(f.x) - cx;
    const int dy = static_cast<int>(f.y) - cy;
    if (dx * dx + dy * dy > R * R) {
      f.vx = -f.vx;
      f.vy = -f.vy;
    }
  }

  for (size_t i = 0; i < sizeof(s_ripples) / sizeof(s_ripples[0]); ++i) {
    if (s_ripples[i] > 0.f) {
      s_ripples[i] += 2.5f + level * 2.f;
    }
  }
  if (level > 0.28f && s_ripples[0] <= 0.f) {
    for (size_t i = sizeof(s_ripples) / sizeof(s_ripples[0]) - 1; i > 0; --i) {
      s_ripples[i] = s_ripples[i - 1];
    }
    s_ripples[0] = 8.f;
  }

  if (!s_cells_init) {
    for (size_t i = 0; i < sizeof(s_cells) / sizeof(s_cells[0]); ++i) {
      s_cells[i] = static_cast<uint8_t>((i * 47) % 256);
    }
    s_cells_init = true;
  }
  const int n = static_cast<int>(sizeof(s_cells) / sizeof(s_cells[0]));
  uint8_t next[72];
  for (int i = 0; i < n; ++i) {
    const int l = (i + n - 1) % n;
    const int r = (i + 1) % n;
    const int sum = s_cells[l] + s_cells[i] + s_cells[r];
    next[i] = static_cast<uint8_t>((sum > 380) ? 0 : (sum < 120 ? 255 : s_cells[i]));
  }
  memcpy(s_cells, next, static_cast<size_t>(n));

  for (int i = 0; i < 120; ++i) {
    s_phy_bright[i] *= 0.92f;
  }
  if (level > 0.2f) {
    const int seed = static_cast<int>(level * 80.f) % 120;
    s_phy_bright[seed] = level;
  }
}

const char *pm_face_spectrum_viz_label(int mode) {
  if (mode < 0 || mode >= PM_SPECTRUM_VIZ_COUNT) {
    return "viz";
  }
  return k_labels[mode];
}

void pm_face_spectrum_viz_draw(int mode, const PmSpectrumVizCtx &c) {
  draw_background(c.cx, c.cy, c.R);
  const float clock_hue = clock_hue_deg();

  switch (static_cast<VizId>(mode)) {
    case VizId::Mandala:
      draw_time_bead(c, clock_hue);
      draw_spec_history(c, 200.f, c.R - 48, c.R - 108);
      draw_spectrum_ring(c, c.R - 6, c.R - 44);
      draw_scope_ring(c, c.R - 118, 28);
      draw_petals(c, 52);
      break;
    case VizId::SpectrumRing:
      draw_spectrum_ring(c, c.R - 8, kR_center + 8);
      break;
    case VizId::Oscilloscope:
      draw_scope_ring(c, c.R - 58, 46);
      break;
    case VizId::Spectrogram:
      draw_spec_history(c, 180.f, c.R - 10, kR_center);
      break;
    case VizId::Petals:
      draw_petals(c, c.R - 70);
      break;
    case VizId::Lissajous:
      draw_lissajous(c);
      break;
    case VizId::BreathOrb:
      draw_breath_orb(c);
      break;
    case VizId::BreathHalo:
      draw_breath_halo(c);
      break;
    case VizId::Rose:
      draw_rose(c);
      break;
    case VizId::Spirograph:
      draw_spirograph(c);
      break;
    case VizId::Phyllotaxis:
      draw_phyllotaxis(c);
      break;
    case VizId::Plasma:
      draw_plasma(c);
      break;
    case VizId::Firefly:
      draw_fireflies(c);
      break;
    case VizId::PulseRipple:
      draw_ripples(c);
      draw_breath_orb(c);
      break;
    case VizId::Starfield:
      draw_starfield(c);
      break;
    case VizId::Kaleidoscope:
      draw_kaleidoscope(c);
      break;
    case VizId::Cellular:
      draw_cellular_ring(c);
      break;
    default:
      break;
  }
}
