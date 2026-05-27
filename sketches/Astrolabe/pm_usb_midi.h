#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Register and start the class-compliant USB MIDI interface when enabled for this build. */
bool pm_usb_midi_begin(void);

/** True when this firmware was built with USB MIDI support. */
bool pm_usb_midi_enabled(void);

/** True when the USB MIDI interface is ready and mounted by a host. */
bool pm_usb_midi_has_sink(void);

/** Send a MIDI note-on event on channel 1. */
bool pm_usb_midi_note_on(uint8_t note, uint8_t velocity);

/** Send a MIDI note-off event on channel 1. */
bool pm_usb_midi_note_off(uint8_t note);

/** Send a MIDI note-on event on a zero-based MIDI channel. */
bool pm_usb_midi_note_on_channel(uint8_t channel, uint8_t note, uint8_t velocity);

/** Send a MIDI note-off event on a zero-based MIDI channel. */
bool pm_usb_midi_note_off_channel(uint8_t channel, uint8_t note);
