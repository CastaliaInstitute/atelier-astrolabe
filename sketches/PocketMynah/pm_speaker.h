#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

enum class PmSpeakerStatus : int8_t { Idle = 0, Playing = 1, DoneOk = 2, DoneFail = -1 };

/** Start MP3 playback on the speaker task (non-blocking). */
void pm_speaker_play_begin(const uint8_t *mp3, size_t mp3_len);

/** Poll playback; call from loop() while Playing. */
PmSpeakerStatus pm_speaker_poll();

/** Estimated playback progress 0..1 while Playing (char-timed fallback). */
float pm_speaker_play_progress(void);

/** Block until playback finishes or times out. */
bool pm_speaker_play_mp3(const uint8_t *mp3, size_t mp3_len);
