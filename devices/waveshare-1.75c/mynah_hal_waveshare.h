#pragma once

#include "display_waveshare_amoled.h"
#include "touch_waveshare.h"

namespace mynah {

inline Display* hal_display() {
  return &waveshare::display_instance();
}

inline Touch* hal_touch() {
  return &waveshare::touch_instance();
}

/** IMU not wired on 1.75C SKU — stub until BOM adds sensor. */
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
