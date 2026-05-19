#pragma once

#include <cstdint>

struct tm;

void pm_face_cycle_draw(const struct tm *tm_local, bool valid_local);
void pm_face_cycle_flash_confirm(uint32_t until_ms);
bool pm_face_cycle_confirm_active(uint32_t now_ms);
/** After first period log (tap); next draw is lightweight before full ring. */
void pm_face_cycle_on_period_logged(void);
/** Paint full ring after confirm pulse (avoids WDT on same frame as NVS save). */
void pm_face_cycle_schedule_full_ring(uint32_t at_ms);
bool pm_face_cycle_take_full_ring_scheduled(uint32_t now_ms);
