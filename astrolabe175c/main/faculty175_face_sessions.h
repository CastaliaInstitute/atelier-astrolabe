#pragma once

#include <stddef.h>
#include <stdint.h>

#include "faculty175_board.h"

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_face_session_draw(bool journal,
                                  faculty175_ui_state_t state,
                                  const char *detail,
                                  uint32_t anim_ms,
                                  const uint8_t *waveform,
                                  const uint8_t *waveform_stream,
                                  size_t waveform_len);

void faculty175_face_journal_draw(uint32_t anim_ms);
void faculty175_face_conversation_draw(uint32_t anim_ms);

#ifdef __cplusplus
}
#endif
