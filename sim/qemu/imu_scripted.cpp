#include "imu_scripted.h"

namespace mynah::qemu {

static ImuScripted g_imu;

ImuScripted& imu_instance() { return g_imu; }

bool ImuScripted::begin() { return true; }

bool ImuScripted::read(ImuSample* out) {
  if (!out) {
    return false;
  }
  *out = {};
  return true;
}

}  // namespace mynah::qemu
