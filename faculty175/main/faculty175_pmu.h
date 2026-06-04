#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Enable Waveshare AXP2101 rails (MIC BLDO2, display BLDO1, core DC3, etc.). */
esp_err_t faculty175_pmu_init(void);

#ifdef __cplusplus
}
#endif
