#pragma once

#include <cstddef>

#include "faces/pm_face_descriptor.h"

size_t pm_face_registry_count(void);
const PmFaceDescriptor *pm_face_registry_at(size_t index);
const PmFaceDescriptor *pm_face_registry_find(ClockFace face);
bool pm_face_registry_has_flag(ClockFace face, PmFaceFlags flag);
