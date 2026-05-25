#include "faces/globe/pm_face_globe.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "faces/globe/globe_texture.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"

#if defined(ASTROLABE_P4_TARGET)
#include <png.h>

#include "esp_heap_caps.h"
#endif

static uint32_t s_last_ms = 0;
static float s_spin = 0.f;
static uint16_t *s_live_texture = nullptr;
static int s_live_texture_w = 0;
static int s_live_texture_h = 0;
static char s_live_texture_source[192] = "";

bool pm_face_globe_anim_tick(uint32_t now_ms) {
  if (s_last_ms == 0) {
    s_last_ms = now_ms;
    return true;
  }
  const uint32_t dt = now_ms - s_last_ms;
  if (dt < 250u) {
    return false;
  }
  s_last_ms = now_ms;
  s_spin += static_cast<float>(dt) * 0.00018f;
  if (s_spin > 6.2831853f) {
    s_spin -= 6.2831853f;
  }
  return true;
}

static float clamp01(float v) {
  if (v < 0.f) {
    return 0.f;
  }
  return v > 1.f ? 1.f : v;
}

static uint16_t globe_sample_rgb565(float lon, float lat, float shade) {
  const uint16_t *texture = s_live_texture ? s_live_texture : kGlobeTextureRgb565;
  const int texture_w = s_live_texture ? s_live_texture_w : kGlobeTextureW;
  const int texture_h = s_live_texture ? s_live_texture_h : kGlobeTextureH;
  const float u = lon / 6.2831853f;
  float wrapped = u - floorf(u);
  int tx = static_cast<int>(wrapped * static_cast<float>(texture_w));
  int ty = static_cast<int>((0.5f - lat / 3.14159265f) * static_cast<float>(texture_h));
  if (tx < 0) {
    tx = 0;
  } else if (tx >= texture_w) {
    tx = texture_w - 1;
  }
  if (ty < 0) {
    ty = 0;
  } else if (ty >= texture_h) {
    ty = texture_h - 1;
  }
  const uint16_t src = texture[ty * texture_w + tx];
  uint8_t r = static_cast<uint8_t>(((src >> 11) & 0x1F) * 255 / 31);
  uint8_t g = static_cast<uint8_t>(((src >> 5) & 0x3F) * 255 / 63);
  uint8_t b = static_cast<uint8_t>((src & 0x1F) * 255 / 31);
  shade = clamp01(shade);
  r = static_cast<uint8_t>(r * shade + 2.f);
  g = static_cast<uint8_t>(g * shade + 5.f);
  b = static_cast<uint8_t>(b * shade + 12.f);
  return pm_gfx->color565(r, g, b);
}

#if defined(ASTROLABE_P4_TARGET)
extern "C" bool astrolabe_real_ui_globe_load_png(const char *path, const char *source_url) {
  if (!path || path[0] == '\0') {
    return false;
  }
  FILE *fp = std::fopen(path, "rb");
  if (!fp) {
    return false;
  }
  uint8_t sig[8] = {};
  if (std::fread(sig, 1, sizeof(sig), fp) != sizeof(sig) || png_sig_cmp(sig, 0, sizeof(sig)) != 0) {
    std::fclose(fp);
    return false;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    std::fclose(fp);
    return false;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    std::fclose(fp);
    return false;
  }

  bool ok = false;
  uint16_t *next_texture = nullptr;
  png_bytep row = nullptr;
  if (setjmp(png_jmpbuf(png_ptr))) {
    ok = false;
  } else {
    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(sig));
    png_read_info(png_ptr, info_ptr);

    const int w = static_cast<int>(png_get_image_width(png_ptr, info_ptr));
    const int h = static_cast<int>(png_get_image_height(png_ptr, info_ptr));
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (w >= 360 && w <= 2048 && h >= 180 && h <= 1024) {
      if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
      }
      if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
      }
      if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
      }
      if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
      }
      if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
      }
      if ((color_type & PNG_COLOR_MASK_ALPHA) == 0 && !png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
      }

      png_read_update_info(png_ptr, info_ptr);
      const size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
      const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
      next_texture = static_cast<uint16_t *>(
          heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!next_texture) {
        next_texture = static_cast<uint16_t *>(heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_8BIT));
      }
      row = static_cast<png_bytep>(std::malloc(rowbytes));
      if (next_texture && row) {
        for (int y = 0; y < h; ++y) {
          png_read_row(png_ptr, row, nullptr);
          for (int x = 0; x < w; ++x) {
            const png_bytep p = row + x * 4;
            next_texture[y * w + x] = pm_gfx->color565(p[0], p[1], p[2]);
          }
        }
        png_read_end(png_ptr, nullptr);
        if (s_live_texture) {
          heap_caps_free(s_live_texture);
        }
        s_live_texture = next_texture;
        s_live_texture_w = w;
        s_live_texture_h = h;
        next_texture = nullptr;
        if (source_url && source_url[0] != '\0') {
          strlcpy(s_live_texture_source, source_url, sizeof(s_live_texture_source));
        } else {
          strlcpy(s_live_texture_source, path, sizeof(s_live_texture_source));
        }
        ok = true;
      }
    }
  }

  if (row) {
    std::free(row);
  }
  if (next_texture) {
    heap_caps_free(next_texture);
  }
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(fp);
  return ok;
}
#else
extern "C" bool astrolabe_real_ui_globe_load_png(const char *, const char *) { return false; }
#endif

void pm_face_globe_draw(const struct tm *local, bool valid_time) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int r = (min(LCD_WIDTH, LCD_HEIGHT) * 43) / 100;
  const uint16_t bg = pm_gfx->color565(2, 6, 16);
  pm_gfx->fillScreen(bg);

  const float view_lon = s_spin;
  const float sec_day = valid_time && local ? static_cast<float>(local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec)
                                           : 0.f;
  const float sun_lon = valid_time ? (sec_day / 86400.f) * 6.2831853f - 3.14159265f : view_lon + 0.35f;

  for (int i = 0; i < 72; ++i) {
    const uint32_t n = static_cast<uint32_t>(i * 1664525u + 1013904223u);
    const float a = static_cast<float>(n & 0x3ffu) * (6.2831853f / 1024.f);
    const float rr = static_cast<float>(r) * (1.08f + static_cast<float>((n >> 10) & 0xffu) / 255.f * 0.34f);
    pm_gfx->drawPixel(cx + static_cast<int>(cosf(a) * rr), cy + static_cast<int>(sinf(a) * rr),
                      pm_gfx->color565(120, 145, 185));
  }

  for (int y = -r; y <= r; ++y) {
    for (int x = -r; x <= r; ++x) {
      const float nx = static_cast<float>(x) / static_cast<float>(r);
      const float ny = static_cast<float>(y) / static_cast<float>(r);
      const float d2 = nx * nx + ny * ny;
      if (d2 > 1.f) {
        continue;
      }
      const float nz = sqrtf(1.f - d2);
      const float lon = atan2f(nx, nz) + view_lon;
      const float lat = asinf(-ny);
      const float sun_dot = cosf(lat) * cosf(lon - sun_lon);
      const float day = 0.68f + 0.32f * clamp01(sun_dot);
      const float limb = clamp01(0.60f + 0.40f * nz);
      pm_gfx->drawPixel(cx + x, cy + y, globe_sample_rgb565(lon, lat, day * limb));
    }
  }

  for (int i = 0; i < 5; ++i) {
    pm_gfx->drawCircle(cx, cy, r + i, pm_gfx->color565(24 + i * 16, 54 + i * 18, 86 + i * 22));
  }
  pm_gfx->drawCircle(cx, cy, r, pm_gfx->color565(145, 190, 230));
}
