#pragma once

#include <cstdint>
#include <ctime>

#include "faces/pm_faces.h"

enum PmFaceFlags : uint32_t {
  kPmFaceHiddenFromDial = 1u << 0,
  kPmFaceDrawsOwnBackground = 1u << 1,
  kPmFaceLowGestureBanner = 1u << 2,
  kPmFaceSkipRainbow = 1u << 3,
  kPmFaceNoMinuteRedraw = 1u << 4,
  kPmFaceDisableVoiceInput = 1u << 5,
};

struct PmFaceDrawContext {
  uint16_t bg565;
  const tm *local_time;
  bool time_valid;
  int local_hour;
  int local_min;
};

struct PmFaceDescriptor {
  ClockFace id;
  const char *slug;
  const char *label;
  uint32_t flags;
  void (*draw)(const PmFaceDrawContext &ctx);
  void (*on_enter)(ClockFace from);
  void (*on_leave)(ClockFace to);
};

inline bool pm_face_has_flag(const PmFaceDescriptor *face, PmFaceFlags flag) {
  return face && ((face->flags & static_cast<uint32_t>(flag)) != 0);
}
