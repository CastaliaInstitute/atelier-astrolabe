#pragma once

#include <cstdint>

namespace mynah {

/** Platform-neutral display contract. Faces draw through this, not CO5300/QSPI directly. */
class Display {
 public:
  virtual ~Display() = default;

  virtual bool begin() = 0;
  virtual void end() {}

  /** Width/height in pixels (466×466 on 1.75C). */
  virtual int width() const = 0;
  virtual int height() const = 0;

  /** Optional direct RGB565 framebuffer (canvas backends). Null if draw-only API. */
  virtual uint16_t* framebuffer() { return nullptr; }

  virtual void flush() = 0;

  /** Optional round mask: clip to safe circle before flush (host sim / future LVGL). */
  virtual void set_round_clip(bool enabled) { round_clip_ = enabled; }
  virtual bool round_clip() const { return round_clip_; }

 protected:
  bool round_clip_ = true;
};

}  // namespace mynah
