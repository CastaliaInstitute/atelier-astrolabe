#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

typedef struct {
  bool valid;
  int date_key;
  time_t generated_at;
  char date_label[16];
  char title[56];
  char primary_symbol[40];
  char secondary_symbol[40];
  char theme[96];
  char guidance[112];
  char ritual[88];
  char precision[104];
  uint8_t focus_body;
  uint8_t focus_sign;
  uint8_t moon_percent;
  bool moon_waxing;
} PmRhythmsDailyCard;

/** Build/cache today's compact local card when device time is valid. */
bool pm_rhythms_prefetch_daily_card(void);

/** Clear the RAM card after profile/birth changes. */
void pm_rhythms_invalidate_daily_card(void);

const PmRhythmsDailyCard *pm_rhythms_daily_card_cached(void);
bool pm_rhythms_has_cached_daily_card(void);

/** Card-only packet and narrator prompt for BOOT TTS. */
bool pm_rhythms_build_tts_message(char *buf, size_t cap);
bool pm_rhythms_build_tts_system_prompt(char *buf, size_t cap);
