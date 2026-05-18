#pragma once

#include <stddef.h>

/** Load effective Supabase base URL: NVS dev override if present, else build-time config. */
bool pm_supabase_url_get(char *out, size_t out_cap);

/** Load only the NVS override. Returns false when no override is set. */
bool pm_supabase_url_override_load(char *out, size_t out_cap);

/** Persist a dev Supabase URL override to NVS; empty input clears the override. */
bool pm_supabase_url_override_save(const char *url);

void pm_supabase_url_override_clear(void);
