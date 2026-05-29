#pragma once

#include "pins.h"

/** Board identity string for logs and OTA manifests. */
#define MYNAH_BOARD_ID "waveshare-esp32-s3-touch-amoled-1.75c"

/** Panel: CO5300 over QSPI, 466×466 RGB565 canvas (Arduino_GFX). */
#define MYNAH_DISPLAY_DRIVER "co5300_qspi"

/** Touch: CST92xx on shared I2C bus. */
#define MYNAH_TOUCH_DRIVER "cst92xx"

/** PMIC: AXP2101 (XPowersLib). */
#define MYNAH_PMIC_DRIVER "axp2101"
