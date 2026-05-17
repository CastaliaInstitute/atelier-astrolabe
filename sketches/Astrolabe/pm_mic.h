#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

bool pm_mic_begin();
void pm_mic_stop();

/** One vendor frame of int16 samples (see Waveshare 08_ES7210). Returns false on I2S error. */
bool pm_mic_read_frame(int16_t *out, size_t frame_samples, size_t *bytes_read);

/** Samples per I2S read (Waveshare ES7210 @ 16 kHz, 30 ms). */
size_t pm_mic_frame_samples();
