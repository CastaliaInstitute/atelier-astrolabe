#pragma once

#include <cstddef>
#include <cstdint>

#define ASTROLABE_AUDIO_VIS_BANDS 24
#define ASTROLABE_AUDIO_VIS_WAVE_POINTS 64

void astrolabe_audio_visualizer_reset();
void astrolabe_audio_visualizer_feed_output_pcm(const int16_t *pcm, size_t num_s16, int channels);
void astrolabe_audio_visualizer_get(float *bands, size_t band_count, float *wave, size_t wave_count, float *level);
uint32_t astrolabe_audio_visualizer_blocks();
