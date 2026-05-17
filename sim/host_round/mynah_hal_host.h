#pragma once

#include <mynah_hal/display.h>
#include <mynah_hal/touch.h>

namespace mynah::host {

class DisplayHost : public Display {
 public:
  bool begin() override;
  int width() const override;
  int height() const override;
  uint16_t* framebuffer() override;
  void flush() override;
};

class TouchMouse : public Touch {
 public:
  bool begin() override;
  uint8_t sample(TouchPoint* out, uint8_t max_pts) override;

  void inject(int16_t x, int16_t y, bool down);
};

DisplayHost& display_instance();
TouchMouse& touch_instance();

}  // namespace mynah::host

namespace mynah {

inline Display* hal_display() { return &host::display_instance(); }
inline Touch* hal_touch() { return &host::touch_instance(); }

class ImuNull : public Imu {
 public:
  bool begin() override { return true; }
  bool read(ImuSample*) override { return false; }
};

inline Imu* hal_imu() {
  static ImuNull imu;
  return &imu;
}

inline AudioIn* hal_audio_in() { return nullptr; }
inline AudioOut* hal_audio_out() { return nullptr; }
inline Storage* hal_storage() { return nullptr; }
inline Network* hal_network() { return nullptr; }

}  // namespace mynah
