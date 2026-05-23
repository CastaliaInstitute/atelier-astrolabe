#pragma once

#include <cstdint>

typedef struct {
  int x;
  int y;
  int iWidth;
  int iHeight;
  uint16_t *pPixels;
} JPEGDRAW;

#define RGB565_BIG_ENDIAN 0

class JPEGDEC {
 public:
  int openRAM(uint8_t *, int, int (*)(JPEGDRAW *)) { return 0; }
  int getWidth() { return 0; }
  int getHeight() { return 0; }
  void setPixelType(int) {}
  int decode(int, int, int) { return 0; }
  void close() {}
};
