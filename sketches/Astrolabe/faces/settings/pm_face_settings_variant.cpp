#include "faces/settings/pm_face_settings_variant.h"

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_variant.h"

void pm_face_settings_variant_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(210, 215, 235);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 145);
  const uint16_t c_accent = pm_gfx->color565(170, 220, 210);

  const PmDeviceVariant variant = pm_variant_get();
  pm_face_draw_centered_line("Variant", 56, c_hi, 2, 2);
  pm_face_draw_centered_line(pm_variant_label(variant), 128, c_accent, 2, 3);
  pm_face_draw_centered_line(pm_variant_summary(variant), 204, c_dim, 1, 1);

  pm_face_draw_centered_line("tap to change", 300, c_hi, 1, 1);
  pm_face_draw_centered_line("limits swipe navigation", 332, c_dim, 1, 1);
}
