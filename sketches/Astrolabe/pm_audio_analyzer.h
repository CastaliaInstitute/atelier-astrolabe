#pragma once

#include <stddef.h>
#include <stdint.h>

/** Log-spaced magnitude buckets for polar spectrum / petals. */
#define PM_AUDIO_ANALYZER_BANDS 24

/** Decimated waveform points for circular oscilloscope (one FFT block). */
#define PM_AUDIO_WAVE_POINTS 64

/** Concentric spectrogram history rings (oldest → newest). */
#define PM_AUDIO_SPEC_HISTORY 5

#define PM_AUDIO_ANALYZER_IN_CHANNELS 2

void pm_audio_analyzer_reset(void);

/** Push mono PCM for one ES7210 TDM input channel (16 kHz). */
void pm_audio_analyzer_feed_in_channel(int channel, const int16_t *pcm, size_t num_samples);

/** Push mono/stereo interleaved PCM from speaker path (any rate; mono = ch0). */
void pm_audio_analyzer_feed_out(const int16_t *pcm, size_t num_s16, int channels);

/** Smoothed 0..1 magnitudes (left top/bottom = mic ch0/ch1, right = speaker). */
void pm_audio_analyzer_get_in_low(float *bands, size_t count);
void pm_audio_analyzer_get_in_high(float *bands, size_t count);
void pm_audio_analyzer_get_out(float *bands, size_t count);

/** Per-bin max of mic ch0/ch1 and speaker path (0..1). */
void pm_audio_analyzer_get_mix(float *bands, size_t count);

/** Normalized -1..1 samples around the circle (mic ch0, last FFT block). */
void pm_audio_analyzer_get_waveform(float *samples, size_t count);

/**
 * Spectrogram rings: row 0 = oldest, row (count-1) = newest; each row has `bands` bins 0..1.
 * `count` must be <= PM_AUDIO_SPEC_HISTORY.
 */
void pm_audio_analyzer_get_spec_history(float *rows, int count, int bands);

/** Smoothed 0..1 level for center orb / petal glow. */
float pm_audio_analyzer_get_level(void);

/** Capture mic frame and run input FFTs (Spectrum face). */
void pm_audio_analyzer_tick(void);

#ifndef ASTROLABE_QEMU
bool pm_audio_analyzer_mic_begin(void);
void pm_audio_analyzer_mic_end(void);
#else
static inline bool pm_audio_analyzer_mic_begin(void) { return true; }
static inline void pm_audio_analyzer_mic_end(void) {}
#endif
