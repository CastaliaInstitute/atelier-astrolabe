#pragma once

#include <cstddef>
#include <cstdint>

void pm_face_watcher_on_enter(void);
void pm_face_watcher_on_leave(void);
const char *pm_face_watcher_cycle_mode(void);
const char *pm_face_watcher_mode_label(void);
void pm_face_watcher_format_prompt_state(char *out, size_t cap);
void pm_face_watcher_draw(void);
