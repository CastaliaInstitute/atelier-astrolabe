#include "pm_qr.h"

#include <cstring>

#include "Arduino_GFX_Library.h"

extern "C" {
#include "third_party/qrcodegen/qrcodegen.h"
}

static constexpr int kQrMaxVersion = 12;
static constexpr size_t kQrBufLen = qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion);

static uint8_t s_temp[kQrBufLen];
static uint8_t s_out[kQrBufLen];
static char s_cached_url[128] = "";
static bool s_modules_valid = false;
static int s_cached_size = 0;

void pm_qr_invalidate_cache(void) {
  s_cached_url[0] = '\0';
  s_modules_valid = false;
  s_cached_size = 0;
}

static bool encode_url_cache(const char *url) {
  if (!url || url[0] == '\0') {
    return false;
  }
  if (s_modules_valid && strcmp(url, s_cached_url) == 0 && s_cached_size > 0) {
    return true;
  }
  if (!qrcodegen_encodeText(url, s_temp, s_out, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, kQrMaxVersion,
                            qrcodegen_Mask_AUTO, true)) {
    pm_qr_invalidate_cache();
    return false;
  }
  strncpy(s_cached_url, url, sizeof(s_cached_url) - 1);
  s_cached_url[sizeof(s_cached_url) - 1] = '\0';
  s_modules_valid = true;
  s_cached_size = qrcodegen_getSize(s_out);
  return s_cached_size > 0;
}

bool pm_qr_draw_url(Arduino_Canvas *gfx, const char *url, int cx, int cy, int max_px) {
  if (!gfx || !url || url[0] == '\0' || !encode_url_cache(url)) {
    return false;
  }

  const int size = s_cached_size > 0 ? s_cached_size : qrcodegen_getSize(s_out);
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
      const bool on = qrcodegen_getModule(s_out, x, y);
      int run = 1;
      while (x + run < size && qrcodegen_getModule(s_out, x + run, y) == on) {
        ++run;
      }
      if (on) {
        gfx->fillRect(x0 + x * mod, y0 + y * mod, run * mod, mod, fg);
      }
      x += run;
    }
  }
  return true;
}
