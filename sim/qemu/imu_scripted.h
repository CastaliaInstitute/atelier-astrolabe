#pragma once

#include <mynah_hal/imu.h>

namespace mynah::qemu {

class ImuScripted : public Imu {
 public:
  bool begin() override;
  bool read(ImuSample* out) override;
};

ImuScripted& imu_instance();

}  // namespace mynah::qemu
