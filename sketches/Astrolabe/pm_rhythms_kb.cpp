#include "pm_rhythms_kb.h"

#include <string.h>

size_t pm_rhythms_kb_count() {
  return kPmRhythmsKbEntryCount;
}

const PmRhythmsKbEntry *pm_rhythms_kb_entry_at(size_t index) {
  if (index >= kPmRhythmsKbEntryCount) {
    return nullptr;
  }
  return &kPmRhythmsKbEntries[index];
}

const PmRhythmsKbEntry *pm_rhythms_kb_lookup(const char *interpretation_key) {
  if (!interpretation_key || !interpretation_key[0]) {
    return nullptr;
  }

  size_t lo = 0;
  size_t hi = kPmRhythmsKbEntryCount;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = strcmp(interpretation_key, kPmRhythmsKbEntries[mid].interpretation_key);
    if (cmp == 0) {
      return &kPmRhythmsKbEntries[mid];
    }
    if (cmp > 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return nullptr;
}
