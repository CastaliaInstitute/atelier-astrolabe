#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char slug[32];
  char name[36];
  char last_user[160];
  char last_reply[240];
  bool valid;
} PmFacultyProfile;

static constexpr int kPmFacultySlots = 6;

enum class PmFacultyBustStatus : int8_t { Idle = 0, Working = 1, DoneOk = 2, DoneFail = -1 };

void pm_faculty_ensure_seed(void);
int pm_faculty_count(void);
bool pm_faculty_get_slot(int slot, PmFacultyProfile *out);
bool pm_faculty_active(PmFacultyProfile *out);
bool pm_faculty_set_active_slot(int slot);
bool pm_faculty_cycle_active(int delta, PmFacultyProfile *out);
bool pm_faculty_remember(const char *slug, const char *name);
bool pm_faculty_set_active_slug(const char *slug, const char *name);
void pm_faculty_note_turn(const char *slug, const char *name, const char *transcript, const char *reply);
bool pm_faculty_build_history(char *out, size_t cap);
void pm_faculty_label_from_slug(const char *slug, char *out, size_t cap);

bool pm_faculty_tick_bust_fetch(void);
/** Poll fetch completion, decode JPEG, advance rise animation. Returns true if UI should repaint. */
bool pm_faculty_tick(uint32_t now_ms);
void pm_faculty_begin_bust_rise(void);
bool pm_faculty_bust_animating(void);
/** Draw portrait (JPEG or placeholder) rising from bottom; ~half screen tall. */
void pm_faculty_draw_bust(void);
/** Call when the active faculty slug changes (cycle / set active). */
void pm_faculty_on_active_changed(void);
PmFacultyBustStatus pm_faculty_bust_status(void);
const char *pm_faculty_bust_slug(void);
size_t pm_faculty_bust_size(void);
const char *pm_faculty_bust_last_error(void);
