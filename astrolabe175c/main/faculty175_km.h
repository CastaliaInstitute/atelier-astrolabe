#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "tinyusb.h"

/** True only in the dedicated Wi-Fi keyboard/mouse build. */
bool faculty175_km_enabled(void);

/** USB descriptors for the boot-keyboard + boot-mouse device. */
const tusb_desc_device_t *faculty175_km_device_descriptor(void);
const uint8_t *faculty175_km_configuration_descriptor(void);
const char **faculty175_km_string_descriptors(void);
size_t faculty175_km_string_descriptor_count(void);

/** Start the report worker and create the per-boot pairing code. */
esp_err_t faculty175_km_start(void);

/** Six decimal digits. The code is intentionally shown only on-device/serial. */
const char *faculty175_km_pairing_code(void);

/** Current USB mount state, suitable for the on-device status face. */
bool faculty175_km_usb_ready(void);

/** Queue browser input for delivery to the USB host. */
esp_err_t faculty175_km_mouse(int16_t dx, int16_t dy, int8_t wheel, uint8_t buttons);
esp_err_t faculty175_km_key(uint8_t keycode, bool down, uint8_t modifiers);
esp_err_t faculty175_km_type_text(const char *text);
esp_err_t faculty175_km_release_all(void);

/** Two-step safety action: first call arms, second call within ten seconds injects the Pi installer command. */
esp_err_t faculty175_km_install_pi_agent(void);
bool faculty175_km_install_armed(void);

/** Translate a DOM KeyboardEvent.code value to a USB HID keycode. */
uint8_t faculty175_km_keycode_from_dom(const char *code);
