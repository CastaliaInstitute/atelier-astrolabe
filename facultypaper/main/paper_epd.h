#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAPER_EPD_MODE_QUALITY = 1,
    PAPER_EPD_MODE_TEXT = 2,
    PAPER_EPD_MODE_FAST = 3,
    PAPER_EPD_MODE_FASTEST = 4,
} paper_epd_mode_t;

esp_err_t paper_epd_init(void);
esp_err_t paper_epd_smoke_test(void);
esp_err_t paper_epd_flush_rgb565(const uint16_t *fb, int logical_w, int logical_h);
void paper_epd_set_mode(paper_epd_mode_t mode);
paper_epd_mode_t paper_epd_get_mode(void);
bool paper_epd_ready(void);

#ifdef __cplusplus
}
#endif
