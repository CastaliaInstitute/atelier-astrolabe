#include "pm_midi.h"

#include "pm_rtp_midi.h"
#include "pm_usb_midi.h"

uint8_t pm_midi_channel(PmMidiInstrument instrument) {
  switch (instrument) {
    case PmMidiInstrument::Ocarina:
      return 0;
    case PmMidiInstrument::Kalimba:
      return 1;
    case PmMidiInstrument::Drone:
      return 2;
    case PmMidiInstrument::Chord:
      return 3;
    case PmMidiInstrument::Piano:
      return 4;
    case PmMidiInstrument::PanDrum:
      return 5;
    case PmMidiInstrument::Bongo:
      return 9;
    default:
      return 0;
  }
}

const char *pm_midi_instrument_label(PmMidiInstrument instrument) {
  switch (instrument) {
    case PmMidiInstrument::Ocarina:
      return "ocarina";
    case PmMidiInstrument::Kalimba:
      return "kalimba";
    case PmMidiInstrument::Drone:
      return "drone";
    case PmMidiInstrument::Chord:
      return "chord";
    case PmMidiInstrument::Piano:
      return "piano";
    case PmMidiInstrument::PanDrum:
      return "pandrum";
    case PmMidiInstrument::Bongo:
      return "bongo";
    default:
      return "instrument";
  }
}

bool pm_midi_has_sink(void) { return pm_usb_midi_has_sink() || pm_rtp_midi_has_sink(); }

bool pm_midi_note_on(PmMidiInstrument instrument, uint8_t note, uint8_t velocity) {
  const uint8_t channel = pm_midi_channel(instrument);
  const bool usb_ok = pm_usb_midi_note_on_channel(channel, note, velocity);
  const bool rtp_ok = pm_rtp_midi_note_on_channel(channel, note, velocity);
  return usb_ok || rtp_ok;
}

bool pm_midi_note_off(PmMidiInstrument instrument, uint8_t note) {
  const uint8_t channel = pm_midi_channel(instrument);
  const bool usb_ok = pm_usb_midi_note_off_channel(channel, note);
  const bool rtp_ok = pm_rtp_midi_note_off_channel(channel, note);
  return usb_ok || rtp_ok;
}
