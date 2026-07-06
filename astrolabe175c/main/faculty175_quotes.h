#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ok;
    bool demo;
    char date[16];
    int index;
    int total;
    char faculty_slug[32];
    char faculty_name[48];
    char quote[256];
    char passage[72];
    char book_title[72];
    char book_author[56];
    char error[64];
} faculty175_quote_t;

esp_err_t faculty175_quotes_init(void);
void faculty175_quotes_start_auto_fetch_task(void);
void faculty175_quotes_request_refresh(void);
bool faculty175_quotes_handle(const char *line);
bool faculty175_quotes_current(faculty175_quote_t *out);
const char *faculty175_quotes_state_name(void);
const char *faculty175_quotes_last(void);

#ifdef __cplusplus
}
#endif
