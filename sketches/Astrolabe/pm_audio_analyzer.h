#pragma once

#include <stddef.h>
#include <stdint.h>

/** Display bins per direction (mic in / speaker out). */
#define PM_AUDIO_ANALYZER_BANDS 32

void pm_audio_analyzer_reset(void);

/** Push mono PCM from microphone path (16 kHz typical). */
void pm_audio_analyzer_feed_in(const int16_t *pcm, size_t num_samples);

/** Push mono/stereo interleaved PCM from speaker path (any rate; mono = ch0). */
void pm_audio_analyzer_feed_out(const int16_t *pcm, size_t num_s16, int channels);

/** Smoothed 0..1 magnitudes for drawing (valid after feed + FFT). */
void pm_audio_analyzer_get_in(float *bands, size_t count);
void pm_audio_analyzer_get_out(float *bands, size_t count);

/** Advance mic capture + FFT when the spectrum face is active. */
void pm_audio_analyzer_tick(void);

#ifndef ASTROLABE_QEMU
/** Start/stop ES7210 mic for spectrum face. */
bool pm_audio_analyzer_mic_begin(void);
void pm_audio_analyzer_mic_end(void);
#else
static inline bool pm_audio_analyzer_mic_begin(void) { return true; }
static inline void pm_audio_analyzer_mic_end(void) {}
#endif
