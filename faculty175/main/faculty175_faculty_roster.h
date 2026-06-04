#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FACULTY175_FACULTY_ROSTER_MAX 8

typedef struct {
    char slug[32];
    char name[48];
} faculty175_faculty_roster_entry_t;

/** Seed the default faculty list into NVS when empty or stale. */
void faculty175_faculty_roster_ensure_default(void);

int faculty175_faculty_roster_count(void);

bool faculty175_faculty_roster_get(int index, faculty175_faculty_roster_entry_t *out);

bool faculty175_faculty_roster_active(faculty175_faculty_roster_entry_t *out);

int faculty175_faculty_roster_active_index(void);

bool faculty175_faculty_roster_set_active_index(int index);

/** Advance active faculty (wrap); persists to NVS. Returns new index or -1. */
int faculty175_faculty_roster_cycle_next(void);

/** Cycle roster by `delta` (+1 next, -1 previous). Returns new index or -1. */
int faculty175_faculty_roster_cycle_delta(int delta);
