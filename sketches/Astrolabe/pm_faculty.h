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
/** Demo recents: ensure Castalia slug `a.einstein` is saved and active on first Faculty visit. */
void pm_faculty_ensure_demo_seed(void);
void pm_faculty_prepare_demo_view(void);
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
/** Fetch/caches a bust for an explicit faculty slug without changing the active Faculty face recents. */
bool pm_faculty_request_bust(const char *slug);
/** Background warmup: cache active/recent faculty and an optional quote faculty slug to flash. */
bool pm_faculty_preload_busts(const char *quote_slug);
/** Poll fetch completion, decode JPEG, advance rise animation. Returns true if UI should repaint. */
bool pm_faculty_tick(uint32_t now_ms);
void pm_faculty_begin_bust_rise(void);
bool pm_faculty_bust_animating(void);
/** Draw portrait (JPEG or placeholder) rising from bottom; ~half screen tall. */
void pm_faculty_draw_bust(void);
/** Draw portrait/placeholder for a supplied faculty profile without reading active recents. */
void pm_faculty_draw_bust_for(const PmFacultyProfile *faculty);
/** Draw portrait/placeholder constrained to an explicit rectangle, for tour overlays. */
void pm_faculty_draw_bust_for_at(const PmFacultyProfile *faculty, int cx, int bottom_y, int max_w, int max_h);
/** Draw the active faculty portrait as the primary full-face visual, with no rise animation. */
void pm_faculty_draw_bust_fullscreen(void);
/** Call when the active faculty slug changes (cycle / set active). */
void pm_faculty_on_active_changed(void);
/** Release cached portrait bytes/decoded framebuffer when leaving Faculty. */
void pm_faculty_release_bust_cache(void);
PmFacultyBustStatus pm_faculty_bust_status(void);
const char *pm_faculty_bust_slug(void);
size_t pm_faculty_bust_size(void);
const char *pm_faculty_bust_last_error(void);
bool pm_faculty_bust_ready_for(const char *slug);
