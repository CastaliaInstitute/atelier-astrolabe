#pragma once

#include "pm_rocket.h"

extern PmRocketStatus g_rocket_ui;
extern bool s_rocket_have_data;

void pm_face_rocket_draw(void);
void pm_face_rocket_format_countdown(int64_t net_unix, char *out, size_t cap);
void pm_face_rocket_format_until(int64_t net_unix, char *out, size_t cap);

bool pm_face_rocket_has_stream(void);
bool pm_face_rocket_stream_qr_visible(void);
void pm_face_rocket_set_stream_qr_visible(bool visible);
void pm_face_rocket_toggle_stream_qr(void);
