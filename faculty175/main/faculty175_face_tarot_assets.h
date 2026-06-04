#pragma once

#include <stdint.h>

#define FACULTY175_TAROT_CARD_COUNT 78

typedef struct {
    const char *title;
    const char *roman;
    const char *keyword;
    const char *slug;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} faculty175_tarot_card_t;

const faculty175_tarot_card_t *faculty175_tarot_card_get(int idx);
