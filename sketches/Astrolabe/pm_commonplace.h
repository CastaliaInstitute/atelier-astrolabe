#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

enum class PmCommonplaceStatus : int8_t { Idle = 0, Working = 1, DoneOk = 2, DoneFail = -1 };

/** STT + Directus journal via `mynah-pocket-journal` (non-blocking; poll with pm_commonplace_poll). */
bool pm_commonplace_begin_pcm_journal(const uint8_t *pcm, size_t pcm_len);

/** Save a recorded note to flash for later Commonplace upload. */
bool pm_commonplace_save_offline_note(const uint8_t *pcm, size_t pcm_len);

/** Count queued offline note audio files on flash. */
size_t pm_commonplace_offline_note_count(void);

PmCommonplaceStatus pm_commonplace_poll(void);

/** After DoneOk: STT transcript from last successful journal save. */
const char *pm_commonplace_last_transcript(void);

const char *pm_commonplace_last_error(void);

void pm_commonplace_abort(void);
