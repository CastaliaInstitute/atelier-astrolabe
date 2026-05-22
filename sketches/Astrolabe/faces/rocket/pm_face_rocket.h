#pragma once

#include "pm_rocket.h"

extern PmRocketStatus g_rocket_ui;
extern bool s_rocket_have_data;

void pm_face_rocket_draw(void);
/** Format T- or T+ relative to launch NET (top of ring = T-0). */
void pm_face_rocket_format_t_rel(int64_t launch_unix, char *out, size_t cap);
void pm_face_rocket_format_countdown(int64_t net_unix, char *out, size_t cap);
void pm_face_rocket_format_until(int64_t net_unix, char *out, size_t cap);

bool pm_face_rocket_has_stream(void);
bool pm_face_rocket_stream_qr_visible(void);
void pm_face_rocket_set_stream_qr_visible(bool visible);
void pm_face_rocket_toggle_stream_qr(void);
bool pm_face_rocket_stream_needs_repaint(uint32_t now_ms);
bool pm_face_rocket_needs_repaint(uint32_t now_ms);
bool pm_face_rocket_tap(int16_t x, int16_t y, char *banner, size_t banner_cap);
bool pm_face_rocket_cycle_launch(int delta);
int pm_face_rocket_selected_index(void);
void pm_face_rocket_reset_selection(void);
