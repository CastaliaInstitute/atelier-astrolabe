#pragma once

#include <cstddef>
#include <cstdint>

namespace mynah {

class AudioIn {
 public:
  virtual ~AudioIn() = default;
  virtual bool begin() = 0;
  /** Capture mono PCM (e.g. 16 kHz for voice-pipeline). Returns samples written. */
  virtual size_t read_pcm(int16_t* out, size_t max_samples) = 0;
};

class AudioOut {
 public:
  virtual ~AudioOut() = default;
  virtual bool begin() = 0;
  virtual bool play_mp3(const uint8_t* data, size_t len) = 0;
  virtual void stop() = 0;
};

}  // namespace mynah
