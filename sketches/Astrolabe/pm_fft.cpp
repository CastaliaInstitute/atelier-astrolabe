#include "pm_fft.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Cplx {
  float r;
  float i;
};

static Cplx s_twiddle[PM_FFT_N];
static float s_hann[PM_FFT_N];
static bool s_ready = false;

void pm_fft_init(void) {
  if (s_ready) {
    return;
  }
  for (int k = 0; k < PM_FFT_N; ++k) {
    const float phase = -2.f * static_cast<float>(M_PI) * static_cast<float>(k) / static_cast<float>(PM_FFT_N);
    s_twiddle[k].r = cosf(phase);
    s_twiddle[k].i = sinf(phase);
    s_hann[k] = 0.5f * (1.f - cosf(2.f * static_cast<float>(M_PI) * static_cast<float>(k) /
                                            static_cast<float>(PM_FFT_N - 1)));
  }
  s_ready = true;
}

static void fft_inplace(Cplx *x) {
  // Bit-reverse permutation
  for (int i = 1, j = 0; i < PM_FFT_N; ++i) {
    int bit = PM_FFT_N >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      const Cplx t = x[i];
      x[i] = x[j];
      x[j] = t;
    }
  }
  for (int len = 2; len <= PM_FFT_N; len <<= 1) {
    const int half = len >> 1;
    const int step = PM_FFT_N / len;
    for (int i = 0; i < PM_FFT_N; i += len) {
      for (int j = 0; j < half; ++j) {
        const Cplx w = s_twiddle[j * step];
        const Cplx u = x[i + j];
        const Cplx v = {x[i + j + half].r * w.r - x[i + j + half].i * w.i,
                        x[i + j + half].r * w.i + x[i + j + half].i * w.r};
        x[i + j].r = u.r + v.r;
        x[i + j].i = u.i + v.i;
        x[i + j + half].r = u.r - v.r;
        x[i + j + half].i = u.i - v.i;
      }
    }
  }
}

void pm_fft_compute_magnitude(const int16_t *samples, float *mag_out, size_t mag_count) {
  if (!samples || !mag_out || mag_count == 0) {
    return;
  }
  pm_fft_init();

  static Cplx buf[PM_FFT_N];
  for (int n = 0; n < PM_FFT_N; ++n) {
    const float w = s_hann[n];
    buf[n].r = static_cast<float>(samples[n]) * w;
    buf[n].i = 0.f;
  }
  fft_inplace(buf);

  const size_t nmag = mag_count < static_cast<size_t>(PM_FFT_BINS) ? mag_count : static_cast<size_t>(PM_FFT_BINS);
  for (size_t k = 0; k < nmag; ++k) {
    const float re = buf[k].r;
    const float im = buf[k].i;
    mag_out[k] = sqrtf(re * re + im * im);
  }
}
