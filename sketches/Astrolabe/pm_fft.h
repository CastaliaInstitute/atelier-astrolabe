#pragma once

#include <stddef.h>
#include <stdint.h>

/** Real FFT size (power of two). @ 16 kHz → ~62.5 Hz per bin. */
#define PM_FFT_N 256
#define PM_FFT_BINS (PM_FFT_N / 2)

void pm_fft_init(void);

/** Windowed real input → magnitude of bins 0 .. PM_FFT_BINS-1 (DC at [0]). */
void pm_fft_compute_magnitude(const int16_t *samples, float *mag_out, size_t mag_count);
