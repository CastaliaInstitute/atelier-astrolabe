#pragma once

#include <cstdint>

#include "faces/pm_faces.h"

enum class PmDeviceVariant : uint8_t {
  Pocket = 0,
  Astrolabe,
  Lunasay,
  Ocarina,
  Cameo,
  Luopan,
  Enso,
  kCount,
};

void pm_variant_begin(void);
PmDeviceVariant pm_variant_get(void);
void pm_variant_set(PmDeviceVariant variant);
PmDeviceVariant pm_variant_cycle(int delta);
const char *pm_variant_label(PmDeviceVariant variant);
const char *pm_variant_summary(PmDeviceVariant variant);
const char *pm_variant_device_platform(void);
const char *pm_variant_ota_channel(void);
ClockFace pm_variant_home_face(void);
bool pm_variant_face_allowed(ClockFace face);
