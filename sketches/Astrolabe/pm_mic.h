#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

bool pm_mic_begin();
void pm_mic_stop();

/** Interleaved I2S channels from ES7210 TDM (MIC1–2 → ch0, MIC3–4 → ch1). */
int pm_mic_i2s_channels();
int pm_mic_capture_channels();
int pm_mic_capture_slot(int capture_channel);

/** Mono samples per channel per read (16 kHz, 30 ms). */
size_t pm_mic_frame_samples();

/** Interleaved int16 frame: `frame_samples * pm_mic_i2s_channels()` elements. */
bool pm_mic_read_frame(int16_t *out, size_t frame_samples, size_t *bytes_read);

/** Pick one interleaved channel into `mono` (length `frame_samples`). */
void pm_mic_pick_channel(const int16_t *interleaved, size_t frame_samples, int channel, int16_t *mono);
void pm_mic_pick_capture_channel(const int16_t *interleaved, size_t frame_samples, int capture_channel,
                                 int16_t *mono);
