#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Initialize ES8311 + I2S TX for PCM playback at sample_hz (mono duplicated to stereo when channels==1). */
esp_err_t pm_speaker_pcm_begin(int sample_hz, int channels);

/** Write interleaved int16 PCM (channels * frames samples). */
esp_err_t pm_speaker_pcm_write(const int16_t *pcm, size_t num_s16);

/** Drain DMA and release I2S driver (codec left powered for quick restart). */
void pm_speaker_pcm_end(void);

/** Host volume 0–100 (maps to ES8311). */
void pm_speaker_pcm_set_volume(int volume);

void pm_speaker_pcm_set_mute(bool mute);

/** True while I2S TX path is active for PCM/UAC. */
bool pm_speaker_pcm_active(void);
