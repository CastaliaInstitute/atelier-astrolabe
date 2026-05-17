#include "pm_qr.h"

#include <cstring>

#include "Arduino_GFX_Library.h"

extern "C" {
#include "third_party/qrcodegen/qrcodegen.h"
}

static constexpr int kPmQrMaxVersion = 12;
static constexpr size_t kPmQrBufLen = qrcodegen_BUFFER_LEN_FOR_VERSION(kPmQrMaxVersion);

static uint8_t s_qr_temp[kPmQrBufLen];
static uint8_t s_qr_out[kPmQrBufLen];
static char s_qr_cached_text[384] = "";
static bool s_qr_modules_valid = false;
static int s_qr_cached_size = 0;

static void pm_qr_invalidate() {
  s_qr_cached_text[0] = '\0';
  s_qr_modules_valid = false;
  s_qr_cached_size = 0;
}

static bool pm_qr_encode(const char *text) {
  if (!text || text[0] == '\0') {
    return false;
  }
  if (s_qr_modules_valid && strcmp(text, s_qr_cached_text) == 0 && s_qr_cached_size > 0) {
    return true;
  }
  if (!qrcodegen_encodeText(text, s_qr_temp, s_qr_out, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, kPmQrMaxVersion,
                           qrcodegen_Mask_AUTO, true)) {
    pm_qr_invalidate();
    return false;
  }
  strncpy(s_qr_cached_text, text, sizeof(s_qr_cached_text) - 1);
  s_qr_cached_text[sizeof(s_qr_cached_text) - 1] = '\0';
  s_qr_modules_valid = true;
  s_qr_cached_size = qrcodegen_getSize(s_qr_out);
  return s_qr_cached_size > 0;
}

bool pm_qr_draw(Arduino_Canvas *gfx, int cx, int cy, int max_px, const char *text) {
  if (!gfx || !text || text[0] == '\0' || max_px < 8) {
    return false;
  }
  if (!pm_qr_encode(text)) {
    return false;
  }

  const int size = s_qr_cached_size > 0 ? s_qr_cached_size : qrcodegen_getSize(s_qr_out);
  if (size <= 0) {
    return false;
  }
  int mod = max_px / size;
  if (mod < 2) {
    mod = 2;
  }
  if (mod > 4) {
    mod = 4;
  }
  const int total = mod * size;
  const int x0 = cx - total / 2;
  const int y0 = cy - total / 2;
  const uint16_t fg = gfx->color565(8, 8, 12);
  const uint16_t bg = gfx->color565(248, 248, 252);
  gfx->fillRect(x0, y0, total, total, bg);
  for (int y = 0; y < size; ++y) {
    int x = 0;
    while (x < size) {
      const bool on = qrcodegen_getModule(s_qr_out, x, y);
      int run = 1;
      while (x + run < size && qrcodegen_getModule(s_qr_out, x + run, y) == on) {
        ++run;
      }
      if (on) {
        gfx->fillRect(x0 + x * mod, y0 + y * mod, run * mod, mod, fg);
      }
      x += run;
    }
    if ((y & 3) == 0) {
      yield();
    }
  }
  return true;
}
