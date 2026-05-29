#pragma once

#include <cstddef>
#include <cstdint>

namespace mynah {

/** NVS / SPIFFS / SD abstraction for config and face assets. */
class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool begin() = 0;
  virtual bool get_blob(const char* key, uint8_t* out, size_t* in_out_len) = 0;
  virtual bool put_blob(const char* key, const uint8_t* data, size_t len) = 0;
};

}  // namespace mynah
