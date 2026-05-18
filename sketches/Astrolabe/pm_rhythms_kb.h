#pragma once

#include <stddef.h>

typedef struct {
  const char *interpretation_key;
  const char *category;
  const char *title;
  const char *tags;
  const char *summary;
  const char *invitations;
  const char *cautions;
  const char *practices;
  const char *safety_level;
} PmRhythmsKbEntry;

extern const PmRhythmsKbEntry kPmRhythmsKbEntries[];
extern const size_t kPmRhythmsKbEntryCount;

size_t pm_rhythms_kb_count();
const PmRhythmsKbEntry *pm_rhythms_kb_entry_at(size_t index);
const PmRhythmsKbEntry *pm_rhythms_kb_lookup(const char *interpretation_key);
