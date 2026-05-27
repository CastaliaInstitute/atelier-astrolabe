#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Register and start the USB HID mouse interface when enabled for this build. */
bool pm_usb_hid_begin(void);

/** True when this firmware was built with USB HID mouse support and initialized. */
bool pm_usb_hid_enabled(void);

/** True when the USB HID device is mounted and can accept mouse reports. */
bool pm_usb_hid_ready(void);

/** Send relative mouse movement plus optional wheel/pan deltas. */
bool pm_usb_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel, int8_t pan);

/** Send a mouse click. Button values use the Arduino USBHIDMouse MOUSE_* bit layout. */
bool pm_usb_hid_mouse_click(uint8_t button);

/** Send six signed HID gamepad axes for spatial/6DOF-style control. */
bool pm_usb_hid_gamepad_send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry,
                             uint32_t buttons);
