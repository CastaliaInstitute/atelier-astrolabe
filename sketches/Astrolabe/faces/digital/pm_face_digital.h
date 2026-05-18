#pragma once

#include <cstddef>
#include <cstdint>

struct tm;

void pm_face_digital_draw(const struct tm *tm, bool valid);
