#pragma once

typedef struct {
  int y;
  int iWidth;
} PNGDRAW;

#define PNG_SUCCESS 0
#define PNG_RGB565_BIG_ENDIAN 0

class PNG {
 public:
  int openFLASH(const uint8_t *, int, void *) { return 0; }
  int openRAM(const uint8_t *, int, int (*)(PNGDRAW *)) { return 1; }
  int decode(void *, int) { return 0; }
  void close() {}
  int getWidth() { return 0; }
  int getHeight() { return 0; }
  void getLineAsRGB565(PNGDRAW *, uint16_t *, int, uint32_t) {}
  bool getAlphaMask(PNGDRAW *, uint8_t *, int) { return false; }
};
