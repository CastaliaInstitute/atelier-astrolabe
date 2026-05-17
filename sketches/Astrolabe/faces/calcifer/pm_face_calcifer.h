#pragma once

#include <cstddef>
#include <cstdint>

#include "pm_calcifer.h"

extern PmCalciferStatus g_calcifer_ui;
extern bool s_calcifer_have_data;

void pm_face_calcifer_format_countdown(int64_t end_unix, char *out, size_t cap);
void pm_face_calcifer_format_until(int64_t start_unix, char *out, size_t cap);
void pm_face_calcifer_draw();
