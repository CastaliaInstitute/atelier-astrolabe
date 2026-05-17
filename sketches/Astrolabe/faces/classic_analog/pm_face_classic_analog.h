#pragma once

#include <cstddef>
#include <cstdint>

struct tm;

void pm_face_classic_analog_draw(uint16_t bg565, const struct tm *tm, bool valid);
