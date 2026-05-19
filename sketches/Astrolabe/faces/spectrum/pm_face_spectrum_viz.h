#pragma once

#include <cstdint>

/** Total swipeable visualizer modes on the Spectrum face. */
#define PM_SPECTRUM_VIZ_COUNT 17

struct PmSpectrumVizCtx {
  int cx;
  int cy;
  int R;
  float hue_base;
  float hue_spin;
  const float *mix;
  int n_bands;
  const float *wave;
  int n_wave;
  const float *hist;
  int n_hist_rows;
  float level;
};

void pm_face_spectrum_viz_reset(void);
void pm_face_spectrum_viz_tick(float level);
void pm_face_spectrum_viz_draw(int mode, const PmSpectrumVizCtx &ctx);
const char *pm_face_spectrum_viz_label(int mode);
