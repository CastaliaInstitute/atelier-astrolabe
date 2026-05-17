#include "pm_version.h"

#include <cstdio>
#include <cstring>

#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "pm_build_info.h"
#include "pm_qr.h"

void pm_version_draw(Arduino_Canvas *gfx,
                     void (*draw_centered)(const char *text, int y, uint16_t fg, uint8_t sx, uint8_t sy)) {
  if (!gfx || !draw_centered) {
    return;
  }

  const uint16_t c_hi = gfx->color565(210, 215, 235);
  const uint16_t c_dim = gfx->color565(120, 128, 145);

  draw_centered("VERSION", 36, c_hi, 2, 2);

  char branch_disp[32];
  strncpy(branch_disp, PM_BUILD_BRANCH, sizeof(branch_disp) - 1);
  branch_disp[sizeof(branch_disp) - 1] = '\0';
  if (strlen(PM_BUILD_BRANCH) > sizeof(branch_disp) - 1) {
    const size_t n = sizeof(branch_disp) - 4;
    memcpy(branch_disp, PM_BUILD_BRANCH, n);
    branch_disp[n] = '\0';
    strcat(branch_disp, "...");
  }
  char branch_line[48];
  snprintf(branch_line, sizeof(branch_line), "branch %s%s", branch_disp, PM_BUILD_DIRTY ? " *" : "");
  draw_centered(branch_line, 76, c_hi, 1, 1);

  char sha_line[40];
  snprintf(sha_line, sizeof(sha_line), "commit %s", PM_BUILD_GIT_SHA);
  draw_centered(sha_line, 104, c_dim, 1, 1);

  char date_line[48];
  snprintf(date_line, sizeof(date_line), "built %s", PM_BUILD_DATE);
  draw_centered(date_line, 128, c_dim, 1, 1);

  if (PM_BUILD_QR_URL[0] != '\0') {
    if (!pm_qr_draw(gfx, LCD_WIDTH / 2, 278, 220, PM_BUILD_QR_URL)) {
      draw_centered("QR encode fail", 250, c_dim, 1, 1);
    } else {
      draw_centered("scan for source", 408, c_dim, 1, 1);
    }
  }
}
