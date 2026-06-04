#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t paper_memory_init(void);
bool paper_memory_ready(void);
const char *paper_memory_root(void);
const char *paper_memory_conversation_path(void);
const char *paper_memory_notes_path(void);

typedef struct {
    bool faculty;
    char text[192];
} paper_memory_message_t;

esp_err_t paper_memory_append_turn(uint32_t turn_id,
                                   const char *faculty_slug,
                                   const char *faculty_name,
                                   const char *transcript,
                                   const char *reply);
esp_err_t paper_memory_append_note(const char *source, const char *text);
esp_err_t paper_memory_load_recent_history(char *out, size_t cap);
esp_err_t paper_memory_load_recent_messages(paper_memory_message_t *out, size_t max_messages, size_t *out_count);
esp_err_t paper_memory_clear_history(void);

#ifdef __cplusplus
}
#endif
