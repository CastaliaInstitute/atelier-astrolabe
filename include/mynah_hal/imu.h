#pragma once

namespace mynah {

struct ImuSample {
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  float gx = 0.f;
  float gy = 0.f;
  float gz = 0.f;
};

class Imu {
 public:
  virtual ~Imu() = default;
  virtual bool begin() = 0;
  virtual bool read(ImuSample* out) = 0;
};

}  // namespace mynah
