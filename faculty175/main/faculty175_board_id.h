#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Best-match Waveshare module for the attached hardware. */
typedef enum {
    FACULTY175_GUESS_UNKNOWN = 0,
    /** 466×466 CO5300, ES7210, 32 MB flash — use faculty175 (1.75C). */
    FACULTY175_GUESS_175C,
    /** 466×466 CO5300, ES7210, 16 MB flash — use faculty175; LCD RST on GPIO 2. */
    FACULTY175_GUESS_175,
    /** TCA9554 @ 0x20 — 1.8″ SH8601; flash faculty18 instead. */
    FACULTY175_GUESS_18_WRONG_FW,
} faculty175_board_guess_t;

typedef struct {
    faculty175_board_guess_t guess;
    uint32_t flash_mb;
    uint32_t psram_mb;
    uint8_t mac[6];
    bool axp2101;
    bool tca9554;
    bool es7210;
    bool es8311;
    bool cst9217;
    /** True when flash_mb != configured CONFIG_ESPTOOLPY_FLASHSIZE. */
    bool flash_config_mismatch;
} faculty175_board_identity_t;

/** Probe I2C + flash; log human-readable verdict (call after I2C bus init). */
void faculty175_board_log_identity(void);

const faculty175_board_identity_t *faculty175_board_identity(void);
const char *faculty175_board_guess_name(faculty175_board_guess_t guess);
/** Recommended firmware folder for this hardware (`faculty175` or `faculty18`). */
const char *faculty175_board_recommended_project(void);

#ifdef __cplusplus
}
#endif
