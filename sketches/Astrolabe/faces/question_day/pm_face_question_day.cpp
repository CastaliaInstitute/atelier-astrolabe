#include "faces/question_day/pm_face_question_day.h"

#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_castalia_auth.h"
#include "pm_daily_briefing.h"
#include "pm_display.h"
#include "pm_faculty.h"
#include "pm_wifi_ntp.h"

namespace {

constexpr const char *kDir = "/qotd";
constexpr const char *kCurrentPath = "/qotd/current.txt";
constexpr const char *kFacultyPath = "/qotd/faculty.txt";
constexpr const char *kHistoryPath = "/qotd/history.txt";
constexpr size_t kQuestionCap = 240;
constexpr size_t kHistoryPromptCap = 900;
constexpr uint8_t kMaxHistoryLines = 24;

char s_current[kQuestionCap] = "";
bool s_fs_ready = false;
bool s_loaded = false;
PmFacultyProfile s_faculty = {};
bool s_faculty_loaded = false;

bool fs_begin() {
  if (s_fs_ready) {
    return true;
  }
  if (!LittleFS.begin(true)) {
    return false;
  }
  if (!LittleFS.exists(kDir)) {
    (void)LittleFS.mkdir(kDir);
  }
  s_fs_ready = true;
  return true;
}

void trim_line(char *s) {
  if (!s) {
    return;
  }
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
    memmove(s, s + 1, strlen(s));
  }
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[--n] = '\0';
  }
  if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
    memmove(s, s + 1, n - 2);
    s[n - 2] = '\0';
  }
}

void ensure_loaded() {
  if (s_loaded) {
    return;
  }
  s_loaded = true;
  s_current[0] = '\0';
  if (!fs_begin() || !LittleFS.exists(kCurrentPath)) {
    return;
  }
  File f = LittleFS.open(kCurrentPath, FILE_READ);
  if (!f) {
    return;
  }
  const size_t n = f.readBytes(s_current, sizeof(s_current) - 1);
  s_current[n] = '\0';
  f.close();
  trim_line(s_current);
}

bool read_line(File &f, char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  out[0] = '\0';
  if (!f) {
    return false;
  }
  const String line = f.readStringUntil('\n');
  line.toCharArray(out, cap);
  trim_line(out);
  return out[0] != '\0';
}

void ensure_faculty_loaded() {
  if (s_faculty_loaded) {
    return;
  }
  s_faculty_loaded = true;
  memset(&s_faculty, 0, sizeof(s_faculty));
  if (!fs_begin() || !LittleFS.exists(kFacultyPath)) {
    return;
  }
  File f = LittleFS.open(kFacultyPath, FILE_READ);
  if (!f) {
    return;
  }
  char slug[sizeof(s_faculty.slug)] = "";
  char name[sizeof(s_faculty.name)] = "";
  const bool has_slug = read_line(f, slug, sizeof(slug));
  const bool has_name = read_line(f, name, sizeof(name));
  f.close();
  if (!has_slug || !has_name) {
    return;
  }
  snprintf(s_faculty.slug, sizeof(s_faculty.slug), "%s", slug);
  snprintf(s_faculty.name, sizeof(s_faculty.name), "%s", name);
  s_faculty.valid = true;
}

void save_faculty(const char *slug, const char *name) {
  if (!slug || !slug[0] || !name || !name[0]) {
    return;
  }
  snprintf(s_faculty.slug, sizeof(s_faculty.slug), "%s", slug);
  snprintf(s_faculty.name, sizeof(s_faculty.name), "%s", name);
  s_faculty.valid = true;
  s_faculty_loaded = true;
  if (!fs_begin()) {
    return;
  }
  File f = LittleFS.open(kFacultyPath, FILE_WRITE);
  if (!f) {
    return;
  }
  f.println(s_faculty.slug);
  f.println(s_faculty.name);
  f.close();
}

void append_history(const char *question) {
  if (!question || question[0] == '\0' || !fs_begin()) {
    return;
  }

  char lines[kMaxHistoryLines][kQuestionCap] = {};
  uint8_t count = 0;
  if (LittleFS.exists(kHistoryPath)) {
    File rf = LittleFS.open(kHistoryPath, FILE_READ);
    while (rf && rf.available() && count < kMaxHistoryLines) {
      const String line = rf.readStringUntil('\n');
      line.toCharArray(lines[count], kQuestionCap);
      trim_line(lines[count]);
      if (lines[count][0] != '\0') {
        ++count;
      }
    }
    if (rf) {
      rf.close();
    }
  }

  for (uint8_t i = 0; i < count; ++i) {
    if (strcasecmp(lines[i], question) == 0) {
      return;
    }
  }

  File wf = LittleFS.open(kHistoryPath, FILE_WRITE);
  if (!wf) {
    return;
  }
  const uint8_t start = count >= kMaxHistoryLines ? 1 : 0;
  for (uint8_t i = start; i < count; ++i) {
    wf.println(lines[i]);
  }
  wf.println(question);
  wf.close();
}

void read_history_for_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!fs_begin() || !LittleFS.exists(kHistoryPath)) {
    snprintf(out, cap, "No prior questions stored on this device yet.");
    return;
  }
  File f = LittleFS.open(kHistoryPath, FILE_READ);
  if (!f) {
    snprintf(out, cap, "Question history could not be read.");
    return;
  }
  size_t off = 0;
  uint8_t n = 0;
  while (f.available() && off + 4 < cap) {
    char line[kQuestionCap];
    const String s = f.readStringUntil('\n');
    s.toCharArray(line, sizeof(line));
    trim_line(line);
    if (line[0] == '\0') {
      continue;
    }
    const int wrote = snprintf(out + off, cap - off, "- %s\n", line);
    if (wrote <= 0 || static_cast<size_t>(wrote) >= cap - off) {
      break;
    }
    off += static_cast<size_t>(wrote);
    if (++n >= kMaxHistoryLines) {
      break;
    }
  }
  f.close();
  if (off == 0) {
    snprintf(out, cap, "No prior questions stored on this device yet.");
  }
}

void draw_wrapped(const char *text, int x, int y, int w, int line_h, uint16_t col, uint8_t size) {
  if (!text || !text[0]) {
    return;
  }
  char line[44] = "";
  size_t len = 0;
  const int max_chars = w / (6 * size);
  const char *p = text;
  int yy = y;
  while (*p && yy < LCD_HEIGHT - 44) {
    while (*p == ' ') {
      ++p;
    }
    char word[34];
    size_t wi = 0;
    while (*p && *p != ' ' && wi + 1 < sizeof(word)) {
      word[wi++] = *p++;
    }
    word[wi] = '\0';
    if (wi == 0) {
      break;
    }
    const size_t need = len == 0 ? wi : len + 1 + wi;
    if (need > static_cast<size_t>(max_chars) && len > 0) {
      pm_gfx->setTextColor(col);
      pm_gfx->setTextSize(size);
      pm_gfx->setCursor(x, yy);
      pm_gfx->print(line);
      yy += line_h;
      line[0] = '\0';
      len = 0;
    }
    if (len > 0 && len + 1 < sizeof(line)) {
      line[len++] = ' ';
    }
    strncat(line, word, sizeof(line) - strlen(line) - 1);
    len = strlen(line);
  }
  if (line[0] && yy < LCD_HEIGHT - 44) {
    pm_gfx->setTextColor(col);
    pm_gfx->setTextSize(size);
    pm_gfx->setCursor(x, yy);
    pm_gfx->print(line);
  }
}

void draw_faculty_corner(const PmFacultyProfile &faculty) {
  if (!faculty.valid) {
    return;
  }
  const int max_w = 138;
  const int max_h = 150;
  const int cx = LCD_WIDTH - 74;
  const int bottom_y = LCD_HEIGHT - 6;
  pm_gfx->fillCircle(cx, bottom_y - 58, 72, pm_gfx->color565(11, 15, 22));
  pm_gfx->drawCircle(cx, bottom_y - 58, 72, pm_gfx->color565(74, 82, 92));
  pm_faculty_draw_bust_for_at(&faculty, cx, bottom_y, max_w, max_h, 0, LCD_HEIGHT);
}

void draw_base(const char *status) {
  const uint16_t bg = pm_gfx->color565(9, 12, 18);
  const uint16_t panel = pm_gfx->color565(19, 26, 38);
  const uint16_t ink = pm_gfx->color565(238, 235, 222);
  const uint16_t dim = pm_gfx->color565(130, 150, 166);
  const uint16_t accent = pm_gfx->color565(244, 190, 92);
  const uint16_t blue = pm_gfx->color565(84, 188, 214);

  pm_gfx->fillScreen(bg);
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 56, panel);
  pm_face_draw_centered_line("QUESTION", 10, accent, 2, 2);

  const int cx = LCD_WIDTH / 2;
  const int cy = 178;
  for (int r = 118; r >= 44; r -= 18) {
    pm_gfx->drawCircle(cx, cy, r, pm_gfx->color565(24 + r / 7, 36 + r / 8, 50 + r / 10));
  }
  pm_gfx->fillCircle(cx, cy, 46, pm_gfx->color565(18, 34, 46));
  pm_gfx->drawCircle(cx, cy, 46, blue);
  pm_face_draw_centered_line("?", cy - 25, accent, 5, 5);
  ensure_faculty_loaded();
  draw_faculty_corner(s_faculty);

  ensure_loaded();
  if (s_current[0]) {
    char asked_by[64];
    snprintf(asked_by, sizeof(asked_by), "%s asks", s_faculty.name[0] ? s_faculty.name : "Faculty");
    pm_face_draw_centered_line(asked_by, 290, blue, 1, 1);
    draw_wrapped(s_current, 28, 314, LCD_WIDTH - 152, 22, ink, 1);
  } else {
    pm_face_draw_centered_line("tap or BOOT for a faculty question", 326, ink, 1, 1);
    pm_face_draw_centered_line("then hold PWR to answer", 352, dim, 1, 1);
  }

  if (status && status[0]) {
    pm_face_draw_centered_line(status, 402, blue, 1, 1);
  } else {
    pm_face_draw_centered_line(pm_castalia_has_session() ? "answers sync to Commonplace" : "sign in to sync", 402,
                               dim, 1, 1);
  }
}

bool extract_field(const char *text, const char *key, char *out, size_t cap) {
  if (!text || !key || !out || cap == 0) {
    return false;
  }
  out[0] = '\0';
  const char *p = strstr(text, key);
  if (!p) {
    return false;
  }
  p += strlen(key);
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  size_t n = 0;
  while (*p && *p != '\r' && *p != '\n' && n + 1 < cap) {
    out[n++] = *p++;
  }
  out[n] = '\0';
  trim_line(out);
  return out[0] != '\0';
}

}  // namespace

void pm_face_question_day_draw(void) {
  draw_base(nullptr);
}

void pm_face_question_day_draw_recording(float progress, uint32_t elapsed_ms) {
  draw_base("listening");
  progress = progress < 0.f ? 0.f : (progress > 1.f ? 1.f : progress);
  const uint16_t red = pm_gfx->color565(238, 70, 88);
  const int cx = LCD_WIDTH / 2;
  const int cy = 178;
  const int r = 136;
  const int steps = static_cast<int>(progress * 220.f);
  const int n = steps < 2 ? 2 : steps;
  int x0 = 0;
  int y0 = 0;
  bool have0 = false;
  for (int i = 0; i <= n; ++i) {
    const float a = -pm_face_k_pi * 0.5f + progress * pm_face_k_two_pi *
                                           (static_cast<float>(i) / static_cast<float>(n));
    const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r)));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r)));
    if (have0) {
      pm_gfx->drawLine(x0, y0, x, y, red);
      pm_gfx->drawLine(x0 + 1, y0, x + 1, y, red);
    }
    x0 = x;
    y0 = y;
    have0 = true;
  }
  char t[16];
  const uint32_t sec = elapsed_ms / 1000u;
  snprintf(t, sizeof(t), "%02u:%02u", static_cast<unsigned>(sec / 60u), static_cast<unsigned>(sec % 60u));
  pm_face_draw_centered_line(t, 238, red, 2, 2);
}

void pm_face_question_day_draw_voice_screen(const char *status, float thinking_progress) {
  draw_base(status);
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  } else {
    pm_face_draw_voice_waves_overlay(true, millis());
  }
  pm_gfx->flush();
}

const char *pm_face_question_day_current(void) {
  ensure_loaded();
  return s_current;
}

bool pm_face_question_day_faculty(PmFacultyProfile *out) {
  if (!out) {
    return false;
  }
  ensure_faculty_loaded();
  *out = s_faculty;
  return out->valid;
}

bool pm_face_question_day_set_current(const char *question) {
  if (!question || question[0] == '\0') {
    return false;
  }
  char parsed_question[kQuestionCap] = "";
  char parsed_slug[sizeof(s_faculty.slug)] = "";
  char parsed_name[sizeof(s_faculty.name)] = "";
  const bool structured = extract_field(question, "QUESTION:", parsed_question, sizeof(parsed_question));
  (void)extract_field(question, "FACULTY_SLUG:", parsed_slug, sizeof(parsed_slug));
  (void)extract_field(question, "FACULTY_NAME:", parsed_name, sizeof(parsed_name));
  if (parsed_slug[0] && parsed_name[0]) {
    save_faculty(parsed_slug, parsed_name);
  }

  char clean[kQuestionCap];
  strncpy(clean, structured ? parsed_question : question, sizeof(clean) - 1);
  clean[sizeof(clean) - 1] = '\0';
  trim_line(clean);
  if (clean[0] == '\0') {
    return false;
  }
  if (clean[strlen(clean) - 1] != '?' && strlen(clean) + 1 < sizeof(clean)) {
    strncat(clean, "?", sizeof(clean) - strlen(clean) - 1);
  }
  strncpy(s_current, clean, sizeof(s_current) - 1);
  s_current[sizeof(s_current) - 1] = '\0';
  s_loaded = true;
  append_history(s_current);
  if (!fs_begin()) {
    return true;
  }
  File f = LittleFS.open(kCurrentPath, FILE_WRITE);
  if (!f) {
    return true;
  }
  f.print(s_current);
  f.close();
  return true;
}

bool pm_face_question_day_build_prompt(char *msg, size_t msg_cap, char *sys, size_t sys_cap) {
  if (!msg || !sys || msg_cap < 64 || sys_cap < 64) {
    return false;
  }
  static constexpr size_t kFactsCap = 8192;
  char *facts = static_cast<char *>(heap_caps_malloc(kFactsCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!facts) {
    facts = static_cast<char *>(malloc(kFactsCap));
  }
  if (!facts) {
    return false;
  }
  if (!pm_daily_briefing_build_device_facts(facts, kFactsCap)) {
    snprintf(facts, kFactsCap, "No full device fact bundle is available. WiFi=%s time=%s.",
             pm_wifi_connected() ? "connected" : "offline", pm_time_valid() ? "synced" : "unsynced");
  }
  char history[kHistoryPromptCap];
  read_history_for_prompt(history, sizeof(history));
  snprintf(sys, sys_cap,
           "You write the Mynah Astrolabe Question of the Day. Choose the Castalia faculty member whose "
           "perspective best fits the question you are asking. Use exactly one of these faculty entries: "
           "a.einstein / Einstein for physics, time, pattern, wonder; marie-curie / Marie Curie for experiment, "
           "care, materials, persistence; hypatia / Hypatia for mathematics, philosophy, civic clarity. Use the "
           "device facts, but do not expose implementation details. Do not repeat or closely paraphrase any prior "
           "question. Return exactly three lines in this format: FACULTY_SLUG: <slug> then FACULTY_NAME: <name> "
           "then QUESTION: <one personal answerable question under 120 characters>.");
  snprintf(msg, msg_cap,
           "Device facts:\n%s\n\nPrior questions to avoid:\n%s\n\nSelect the most relevant faculty from the "
           "allowed list and write today's one question.",
           facts, history);
  free(facts);
  return true;
}

bool pm_face_question_day_build_answer_system_prompt(char *sys, size_t sys_cap) {
  if (!sys || sys_cap < 64) {
    return false;
  }
  ensure_loaded();
  snprintf(sys, sys_cap,
           "The user is answering this Question of the Day: \"%s\". Transcribe their answer faithfully. Reply "
           "with one short acknowledgement, no advice unless asked. This route is logged to Commonplace.",
           s_current[0] ? s_current : "What wants your attention today?");
  return true;
}
