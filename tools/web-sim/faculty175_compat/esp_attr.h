#pragma once

/* Host-side equivalents for the ESP-IDF placement attributes used by the
 * shared face sources. WebAssembly has one linear memory, so these are no-ops. */
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define EXT_RAM_BSS_ATTR
