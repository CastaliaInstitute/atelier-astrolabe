#pragma once

#include <stddef.h>

/** Load wearer name from NVS (seeds demo default on first boot). */
void pm_user_begin(void);

/** Display name for voice and UI (never null; empty only before begin). */
const char *pm_user_display_name(void);

/** Honorific form, e.g. "Mr Daniel" (static buffer). */
const char *pm_user_formal_name(void);

/** Persist a new name (1..32 chars). Returns false if invalid. */
bool pm_user_set_name(const char *name);

/** Serial: `name` (print) or `name Daniel`. Returns true if handled. */
bool pm_user_serial_command(const char *line);
