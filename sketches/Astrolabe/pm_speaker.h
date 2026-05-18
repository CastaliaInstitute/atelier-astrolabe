#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

enum class PmSpeakerStatus : int8_t { Idle = 0, Playing = 1, DoneOk = 2, DoneFail = -1 };

/** Start MP3 playback on the speaker task (non-blocking). Waits for prior playback to finish. */
bool pm_speaker_play_begin(const uint8_t *mp3, size_t mp3_len);

/** Poll playback; call from loop() while Playing. */
PmSpeakerStatus pm_speaker_poll();

/** Estimated playback progress 0..1 while Playing (char-timed fallback). */
float pm_speaker_play_progress(void);

/** Block until playback finishes or times out. */
bool pm_speaker_play_mp3(const uint8_t *mp3, size_t mp3_len);

/** Force poll() to leave Playing (does not stop the speaker task immediately). */
void pm_speaker_abort(void);

/** Start a solfeggio-style sine tone (non-blocking). Replaces any current playback. */
bool pm_speaker_play_tone_begin(float hz, uint32_t duration_ms);

/** Loop tone until pm_speaker_tone_stop(). */
bool pm_speaker_play_tone_loop_begin(float hz);

/** Request stop of a looped tone (blocks until speaker task exits). */
void pm_speaker_tone_stop(void);

/** True while MP3, tone, or bowl voice playback is active. */
bool pm_speaker_is_playing(void);

/** Per-frame bowl resonator control (call from touch loop on the bowl face). */
struct PmBowlVoiceCtrl {
  float target_hz = 256.f;
  float excitation = 0.f;
  float brightness = 0.5f;
  float pan = 0.f;
  float rim_quality = 0.f;
  bool finger_down = false;
  bool center_strike = false;
};

/** Start or feed the sustained bowl additive synth (non-blocking). */
void pm_speaker_bowl_voice_push(const PmBowlVoiceCtrl &ctrl);

/** Request immediate fade-out of the bowl voice. */
void pm_speaker_bowl_voice_stop(void);

/** Current resonator energy 0..1 (for visuals). */
float pm_speaker_bowl_voice_energy(void);

/** True while the bowl voice task is sounding or decaying. */
bool pm_speaker_bowl_voice_active(void);
