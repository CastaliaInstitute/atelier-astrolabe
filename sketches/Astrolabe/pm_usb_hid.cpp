#include "pm_usb_hid.h"

#include "sdkconfig.h"

#if defined(ASTROLABE_USB_HID_ENABLED) && CONFIG_TINYUSB_ENABLED && CONFIG_TINYUSB_HID_ENABLED && \
    !defined(ASTROLABE_QEMU)

#include "Arduino.h"
#include "USB.h"
#include "USBHID.h"
#include "USBHIDGamepad.h"
#include "USBHIDMouse.h"

static USBHIDMouse s_mouse;
static USBHIDGamepad s_gamepad;
static USBHID s_hid;
static bool s_ready = false;

bool pm_usb_hid_begin(void) {
  if (s_ready) {
    return true;
  }

  USB.VID(0x303A);
  USB.PID(0x8002);
  USB.manufacturerName("Castalia Institute");
  USB.productName("Astrolabe HID Touchpad");
  USB.serialNumber("astrolabe-hid");

  s_mouse.begin();
  s_gamepad.begin();
  s_hid.begin();
  s_ready = USB.begin();
  return s_ready;
}

bool pm_usb_hid_enabled(void) { return true; }

bool pm_usb_hid_ready(void) { return s_ready && s_hid.ready(); }

bool pm_usb_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
  if (!pm_usb_hid_ready()) {
    return false;
  }
  s_mouse.move(dx, dy, wheel, pan);
  return true;
}

bool pm_usb_hid_mouse_click(uint8_t button) {
  if (!pm_usb_hid_ready()) {
    return false;
  }
  s_mouse.click(button);
  return true;
}

bool pm_usb_hid_gamepad_send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry,
                             uint32_t buttons) {
  if (!pm_usb_hid_ready()) {
    return false;
  }
  return s_gamepad.send(x, y, z, rz, rx, ry, 0, buttons);
}

#else

bool pm_usb_hid_begin(void) { return false; }
bool pm_usb_hid_enabled(void) { return false; }
bool pm_usb_hid_ready(void) { return false; }
bool pm_usb_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
  (void)dx;
  (void)dy;
  (void)wheel;
  (void)pan;
  return false;
}
bool pm_usb_hid_mouse_click(uint8_t button) {
  (void)button;
  return false;
}
bool pm_usb_hid_gamepad_send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry,
                             uint32_t buttons) {
  (void)x;
  (void)y;
  (void)z;
  (void)rz;
  (void)rx;
  (void)ry;
  (void)buttons;
  return false;
}

#endif
