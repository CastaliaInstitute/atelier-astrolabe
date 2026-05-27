#pragma once

#include <stdbool.h>
#include <stdint.h>

enum class PmMidiInstrument : uint8_t {
  Ocarina,
  Kalimba,
  Drone,
  Chord,
  Piano,
  PanDrum,
  Bongo,
};

uint8_t pm_midi_channel(PmMidiInstrument instrument);
const char *pm_midi_instrument_label(PmMidiInstrument instrument);
bool pm_midi_has_sink(void);
bool pm_midi_note_on(PmMidiInstrument instrument, uint8_t note, uint8_t velocity);
bool pm_midi_note_off(PmMidiInstrument instrument, uint8_t note);
