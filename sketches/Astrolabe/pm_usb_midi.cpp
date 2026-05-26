#include "pm_usb_midi.h"

#include "sdkconfig.h"

#if defined(ASTROLABE_USB_MIDI_ENABLED) && CONFIG_TINYUSB_ENABLED && CONFIG_TINYUSB_MIDI_ENABLED && \
    !defined(ASTROLABE_QEMU)

#include "Arduino.h"
#include "USB.h"
#include "class/midi/midi.h"
#include "class/midi/midi_device.h"
#include "esp32-hal-tinyusb.h"
#include "tusb.h"

#include <cstring>

static bool s_descriptor_loaded = false;
static bool s_interface_enabled = false;
static bool s_ready = false;

static constexpr uint8_t kMidiChannel = 0;
static constexpr uint8_t kMidiCable = 0;
static constexpr uint8_t kMidiEndpointSize = 64;

extern "C" uint16_t pm_usb_midi_load_descriptor(uint8_t *dst, uint8_t *itf) {
  if (s_descriptor_loaded) {
    return 0;
  }
  s_descriptor_loaded = true;

  const uint8_t str_index = tinyusb_add_string_descriptor("Ocarina MIDI");
  const uint8_t ep_in = tinyusb_get_free_in_endpoint();
  const uint8_t ep_out = tinyusb_get_free_out_endpoint();
  if (ep_in == 0 || ep_out == 0) {
    return 0;
  }

  const uint8_t descriptor[TUD_MIDI_DESC_LEN] = {
      TUD_MIDI_DESCRIPTOR(*itf, str_index, ep_out, static_cast<uint8_t>(0x80 | ep_in), kMidiEndpointSize),
  };
  *itf += 2;
  memcpy(dst, descriptor, TUD_MIDI_DESC_LEN);
  return TUD_MIDI_DESC_LEN;
}

struct PmUsbMidiRegistrar {
  PmUsbMidiRegistrar() {
    if (!s_interface_enabled &&
        tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, pm_usb_midi_load_descriptor) == ESP_OK) {
      s_interface_enabled = true;
    }
  }
};

static PmUsbMidiRegistrar s_registrar;

static bool write_packet(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2) {
  if (!s_ready || !tud_midi_mounted()) {
    return false;
  }
  const uint8_t packet[4] = {
      static_cast<uint8_t>((kMidiCable << 4) | (cin & 0x0f)),
      status,
      data1,
      data2,
  };
  return tud_midi_packet_write(packet);
}

bool pm_usb_midi_begin(void) {
  USB.VID(0x303A);
  USB.PID(0x8001);
  USB.manufacturerName("Castalia Institute");
  USB.productName("Astrolabe Ocarina MIDI");
  USB.serialNumber("astrolabe-ocarina");
  s_ready = s_interface_enabled;
  return s_ready;
}

bool pm_usb_midi_enabled(void) { return true; }

bool pm_usb_midi_note_on(uint8_t note, uint8_t velocity) {
  return write_packet(MIDI_CIN_NOTE_ON, static_cast<uint8_t>(0x90 | kMidiChannel), note, velocity);
}

bool pm_usb_midi_note_off(uint8_t note) {
  return write_packet(MIDI_CIN_NOTE_OFF, static_cast<uint8_t>(0x80 | kMidiChannel), note, 0);
}

#else

bool pm_usb_midi_begin(void) { return false; }
bool pm_usb_midi_enabled(void) { return false; }
bool pm_usb_midi_note_on(uint8_t note, uint8_t velocity) {
  (void)note;
  (void)velocity;
  return false;
}
bool pm_usb_midi_note_off(uint8_t note) {
  (void)note;
  return false;
}

#endif
