#pragma once

#include <stdint.h>

enum PmResourceOwner : uint32_t {
  kPmResourceVoice = 1u << 0,
  kPmResourceBustFetch = 1u << 1,
  kPmResourceAnalyzer = 1u << 2,
  kPmResourceMediaStream = 1u << 3,
};

bool pm_resource_acquire(uint32_t owner, uint32_t conflicts, const char *name);
void pm_resource_release(uint32_t owner, const char *name);
uint32_t pm_resource_owners(void);
