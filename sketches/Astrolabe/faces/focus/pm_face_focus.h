#pragma once

#include <cstdint>

void pm_face_focus_draw();
bool pm_face_focus_toggle();
void pm_face_focus_reset();
int pm_face_focus_cycle_mode(int delta);
bool pm_face_focus_running();
const char *pm_face_focus_mode_label();
