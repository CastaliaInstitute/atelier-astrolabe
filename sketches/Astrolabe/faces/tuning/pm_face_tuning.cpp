#include "faces/tuning/pm_face_tuning.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_audio_analyzer.h"
#include "pm_display.h"

namespace {

constexpr int kHistory = 18;
constexpr int kStaffY = 238;
constexpr int kLineGap = 20;
constexpr int kBottomLineMidi = 64;  // E4, treble staff bottom line.

struct NoteEvent {
  bool valid;
  int midi;
  int cents;
  float level;
  uint32_t t_ms;
  char note[4];
};

NoteEvent s_notes[kHistory];
PmAudioPitch s_pitch = {};
uint32_t s_last_event_ms = 0;
int s_last_midi = -999;
char s_label[16] = "--";
bool s_active = false;

float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

int note_degree(int midi) {
  static const int8_t kPcDegree[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
  const int octave = (midi / 12) - 1;
  int pc = midi % 12;
  if (pc < 0) {
    pc += 12;
  }
  return octave * 7 + kPcDegree[pc];
}

int staff_y_for_midi(int midi) {
  const int base_degree = note_degree(kBottomLineMidi);
  const int dy_steps = note_degree(midi) - base_degree;
  return kStaffY + (2 * kLineGap) - dy_steps * (kLineGap / 2);
}

bool is_sharp_midi(int midi) {
  int pc = midi % 12;
  if (pc < 0) {
    pc += 12;
  }
  return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void push_note(const PmAudioPitch &p, uint32_t now_ms) {
  memmove(&s_notes[0], &s_notes[1], sizeof(NoteEvent) * (kHistory - 1));
  NoteEvent &n = s_notes[kHistory - 1];
  n.valid = true;
  n.midi = p.midi;
  n.cents = p.cents;
  n.level = p.level;
  n.t_ms = now_ms;
  snprintf(n.note, sizeof(n.note), "%s", p.note);
  s_last_event_ms = now_ms;
  s_last_midi = p.midi;
}

uint16_t staff_color(float a) {
  a = clampf(a, 0.f, 1.f);
  return pm_gfx->color565(static_cast<uint8_t>(54.f + 90.f * a), static_cast<uint8_t>(68.f + 112.f * a),
                          static_cast<uint8_t>(82.f + 126.f * a));
}

void draw_staff(void) {
  const uint16_t bg = pm_gfx->color565(7, 10, 16);
  const uint16_t line = pm_gfx->color565(72, 88, 104);
  const uint16_t glow = pm_gfx->color565(14, 24, 36);
  pm_gfx->fillScreen(bg);
  pm_gfx->fillCircle(pm_face_lcd_cx, pm_face_lcd_cy, 210, glow);

  const int x0 = 44;
  const int x1 = LCD_WIDTH - 38;
  for (int i = 0; i < 5; ++i) {
    const int y = kStaffY + i * kLineGap;
    pm_gfx->drawLine(x0, y, x1, y, line);
    pm_gfx->drawLine(x0, y + 1, x1, y + 1, pm_gfx->color565(26, 38, 50));
  }

  pm_gfx->setTextColor(pm_gfx->color565(144, 184, 210));
  pm_gfx->setTextSize(4, 4);
  pm_gfx->setCursor(62, kStaffY + 15);
  pm_gfx->print("&");
}

void draw_ledger_lines(int x, int y, uint16_t color) {
  const int bottom = kStaffY + 4 * kLineGap;
  if (y < kStaffY - kLineGap / 2) {
    for (int ly = kStaffY - kLineGap; ly >= y - 4; ly -= kLineGap) {
      pm_gfx->drawLine(x - 17, ly, x + 17, ly, color);
    }
  } else if (y > bottom + kLineGap / 2) {
    for (int ly = bottom + kLineGap; ly <= y + 4; ly += kLineGap) {
      pm_gfx->drawLine(x - 17, ly, x + 17, ly, color);
    }
  }
}

void draw_note_event(const NoteEvent &n, int x, float alpha, bool current) {
  if (!n.valid || alpha <= 0.f) {
    return;
  }
  const int y = staff_y_for_midi(n.midi);
  const float tune = 1.f - clampf(fabsf(static_cast<float>(n.cents)) / 50.f, 0.f, 1.f);
  const uint16_t col = current ? pm_gfx->color565(236, 246, 224)
                               : pm_gfx->color565(static_cast<uint8_t>(124.f + 82.f * tune),
                                                  static_cast<uint8_t>(168.f + 62.f * tune),
                                                  static_cast<uint8_t>(196.f + 34.f * tune));
  draw_ledger_lines(x, y, staff_color(alpha));
  pm_gfx->fillCircle(x, y, current ? 12 : 10, col);
  pm_gfx->fillCircle(x - 3, y - 3, current ? 5 : 4, pm_gfx->color565(20, 30, 38));
  pm_gfx->drawLine(x + 10, y, x + 10, y - 58, col);
  if (is_sharp_midi(n.midi)) {
    pm_gfx->setTextColor(staff_color(alpha));
    pm_gfx->setTextSize(1, 2);
    pm_gfx->setCursor(x - 27, y - 11);
    pm_gfx->print("#");
  }
}

void draw_meter(const PmAudioPitch &p) {
  const int cx = pm_face_lcd_cx;
  const int y = 384;
  const int w = 184;
  const uint16_t dim = pm_gfx->color565(70, 82, 94);
  const uint16_t ok = pm_gfx->color565(158, 224, 142);
  const uint16_t warn = pm_gfx->color565(230, 174, 96);
  pm_gfx->drawLine(cx - w / 2, y, cx + w / 2, y, dim);
  for (int i = -50; i <= 50; i += 25) {
    const int x = cx + (i * w) / 100;
    pm_gfx->drawLine(x, y - 7, x, y + 7, i == 0 ? ok : dim);
  }
  if (p.valid) {
    const int cents = p.cents < -50 ? -50 : (p.cents > 50 ? 50 : p.cents);
    const int x = cx + (cents * w) / 100;
    pm_gfx->fillTriangle(x, y - 18, x - 8, y - 2, x + 8, y - 2,
                         fabsf(static_cast<float>(p.cents)) <= 7.f ? ok : warn);
  }
}

}  // namespace

void pm_face_tuning_on_enter(void) {
  memset(s_notes, 0, sizeof(s_notes));
  memset(&s_pitch, 0, sizeof(s_pitch));
  s_last_event_ms = 0;
  s_last_midi = -999;
  snprintf(s_label, sizeof(s_label), "--");
  pm_audio_analyzer_reset();
  (void)pm_audio_analyzer_mic_begin();
  s_active = true;
}

void pm_face_tuning_on_leave(void) {
  if (!s_active) {
    return;
  }
  pm_audio_analyzer_mic_end();
  s_active = false;
}

bool pm_face_tuning_tick(uint32_t now_ms) {
  if (!s_active) {
    return false;
  }
  pm_audio_analyzer_tick();
  pm_audio_analyzer_get_pitch(&s_pitch);
  if (s_pitch.valid) {
    snprintf(s_label, sizeof(s_label), "%s %+d", s_pitch.note, s_pitch.cents);
    const bool new_note = s_pitch.midi != s_last_midi;
    const bool periodic = now_ms - s_last_event_ms >= 360u;
    if (new_note || periodic) {
      push_note(s_pitch, now_ms);
    }
  } else if (now_ms - s_last_event_ms >= 900u) {
    snprintf(s_label, sizeof(s_label), "--");
  }
  return true;
}

void pm_face_tuning_draw(void) {
  const uint32_t now = millis();
  draw_staff();

  for (int i = 0; i < kHistory; ++i) {
    const NoteEvent &n = s_notes[i];
    if (!n.valid) {
      continue;
    }
    const uint32_t age = now >= n.t_ms ? now - n.t_ms : 0;
    const float alpha = 1.f - clampf(static_cast<float>(age) / 7200.f, 0.f, 1.f);
    const int x = LCD_WIDTH - 70 - static_cast<int>((static_cast<uint32_t>(kHistory - 1 - i) * 18u) + age / 42u);
    if (x < 26 || x > LCD_WIDTH + 24) {
      continue;
    }
    draw_note_event(n, x, alpha, i == kHistory - 1 && s_pitch.valid);
  }

  const uint16_t text = pm_gfx->color565(228, 236, 226);
  const uint16_t dim = pm_gfx->color565(128, 148, 162);
  pm_face_draw_centered_line("TUNING", 44, pm_gfx->color565(154, 214, 226), 2, 2);
  pm_face_draw_centered_line(s_pitch.valid ? s_label : "listening", 90, text, 3, 3);

  char hz[32];
  if (s_pitch.valid) {
    snprintf(hz, sizeof(hz), "%.1f Hz", static_cast<double>(s_pitch.hz));
  } else {
    snprintf(hz, sizeof(hz), "mic input");
  }
  pm_face_draw_centered_line(hz, 132, dim, 1, 1);
  draw_meter(s_pitch);
}

const char *pm_face_tuning_note_label(void) { return s_label; }
