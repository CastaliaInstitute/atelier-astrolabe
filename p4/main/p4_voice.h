#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char transcript[512];
  char reply[1024];
  uint8_t *mp3;
  size_t mp3_len;
} astrolabe_p4_voice_result_t;

const char *astrolabe_p4_voice_last_error(void);
void astrolabe_p4_voice_result_free(astrolabe_p4_voice_result_t *result);
bool astrolabe_p4_voice_post_message(const char *message, const char *system_instruction,
                                     astrolabe_p4_voice_result_t *result);
bool astrolabe_p4_voice_post_pcm(const int16_t *pcm, size_t samples, int sample_rate,
                                 const char *system_instruction, astrolabe_p4_voice_result_t *result);
bool astrolabe_p4_voice_speak_message(const char *message, const char *system_instruction);

#ifdef __cplusplus
}
#endif
