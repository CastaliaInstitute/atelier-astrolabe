#pragma once

#include <cstdint>

#ifndef PROGMEM
#define PROGMEM
#endif

#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr) (*(const void *const *)(addr))

