#pragma once

/* ESP placement attributes are intentionally no-ops in WebAssembly. */
#define EXT_RAM_BSS_ATTR
#define EXT_RAM_NOINIT_ATTR
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
