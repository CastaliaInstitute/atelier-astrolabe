#pragma once

#include "display_framebuffer.h"
#include "imu_scripted.h"
#include "touch_scripted.h"

namespace mynah {

inline Display* hal_display() { return &qemu::display_instance(); }
inline Touch* hal_touch() { return &qemu::touch_instance(); }
inline Imu* hal_imu() { return &qemu::imu_instance(); }

inline AudioIn* hal_audio_in() { return nullptr; }
inline AudioOut* hal_audio_out() { return nullptr; }
inline Storage* hal_storage() { return nullptr; }
inline Network* hal_network() { return nullptr; }

}  // namespace mynah
