#pragma once

#include <stddef.h>
#include <stdbool.h>

/** Build astrology / synastry / moon facts for voice-pipeline `briefingFacts`. */
bool pm_daily_briefing_build_device_facts(char *out, size_t cap);
