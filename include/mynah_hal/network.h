#pragma once

namespace mynah {

/** HTTP / Wi-Fi facade for mocked CI (QEMU) vs real stack. */
class Network {
 public:
  virtual ~Network() = default;
  virtual bool begin() = 0;
  virtual bool is_connected() const = 0;
};

}  // namespace mynah
