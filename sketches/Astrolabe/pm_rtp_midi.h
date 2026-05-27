#pragma once

#include <stdbool.h>
#include <stdint.h>

bool pm_rtp_midi_begin(void);
void pm_rtp_midi_tick(void);
bool pm_rtp_midi_enabled(void);
bool pm_rtp_midi_has_sink(void);
bool pm_rtp_midi_note_on(uint8_t note, uint8_t velocity);
bool pm_rtp_midi_note_off(uint8_t note);
bool pm_rtp_midi_note_on_channel(uint8_t channel, uint8_t note, uint8_t velocity);
bool pm_rtp_midi_note_off_channel(uint8_t channel, uint8_t note);
