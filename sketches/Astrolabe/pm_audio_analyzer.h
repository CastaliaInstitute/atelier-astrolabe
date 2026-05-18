#pragma once

#include <stddef.h>
#include <stdint.h>

/** Vertical bars per spectrum panel. */
#define PM_AUDIO_ANALYZER_BANDS 24

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

/** Capture mic frame and run input FFTs (Spectrum face). */
void pm_audio_analyzer_tick(void);

#ifndef ASTROLABE_QEMU
bool pm_audio_analyzer_mic_begin(void);
void pm_audio_analyzer_mic_end(void);
#else
static inline bool pm_audio_analyzer_mic_begin(void) { return true; }
static inline void pm_audio_analyzer_mic_end(void) {}
#endif
