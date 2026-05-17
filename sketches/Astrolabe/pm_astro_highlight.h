#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pm_transit.h"

typedef enum { kPmAstroMentionBody = 0, kPmAstroMentionSign = 1, kPmAstroMentionHouse = 2 } PmAstroMentionKind;

typedef struct {
  PmAstroMentionKind kind;
  uint8_t id;
} PmAstroMention;

typedef struct {
  PmAstroMention items[36];
  uint8_t count;
} PmAstroHighlightPlan;

/** Scan spoken reply text for planets, signs, and houses (in text order). */
void pm_astro_highlight_build(const char *reply, PmAstroHighlightPlan *plan);

/** Pick highlight for normalized playback progress in [0, 1]. */
void pm_astro_highlight_at_progress(const PmAstroHighlightPlan *plan, float progress, int *out_body,
                                    int *out_sign);
