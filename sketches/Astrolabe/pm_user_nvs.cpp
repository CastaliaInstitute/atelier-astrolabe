#include "pm_user_nvs.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pm_config.h"

namespace {

constexpr char kNs[] = "mynah";
constexpr char kKeyName[] = "user_name";
constexpr size_t kNameMax = 32;

char s_name[kNameMax + 1] = "";

bool valid_name_char(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '\'';
}

void trim_inplace(char *s) {
  if (!s) {
    return;
  }
  size_t n = strlen(s);
  while (n > 0 && isspace(static_cast<unsigned char>(s[n - 1]))) {
    s[--n] = '\0';
  }
  size_t start = 0;
  while (s[start] != '\0' && isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  if (start > 0) {
    memmove(s, s + start, strlen(s + start) + 1);
  }
}

bool normalize_name(const char *in, char *out, size_t out_sz) {
  if (!in || !out || out_sz < 2) {
    return false;
  }
  out[0] = '\0';
  size_t o = 0;
  bool last_space = true;
  for (size_t i = 0; in[i] != '\0' && o + 1 < out_sz; ++i) {
    const char c = in[i];
    if (!valid_name_char(c)) {
      continue;
    }
    if (c == ' ') {
      if (last_space) {
        continue;
      }
      last_space = true;
    } else {
      last_space = false;
    }
    out[o++] = c;
  }
  while (o > 0 && out[o - 1] == ' ') {
    --o;
  }
  out[o] = '\0';
  return o > 0;
}

void save_name(void) {
  Preferences pref;
  if (!pref.begin(kNs, false)) {
    return;
  }
  pref.putString(kKeyName, s_name);
  pref.end();
}

}  // namespace

void pm_user_begin(void) {
  strncpy(s_name, MYNAH_USER_NAME_DEFAULT, kNameMax);
  s_name[kNameMax] = '\0';

  Preferences pref;
  if (!pref.begin(kNs, true)) {
    return;
  }
  if (!pref.isKey(kKeyName)) {
    pref.end();
    Preferences wr;
    if (wr.begin(kNs, false)) {
      wr.putString(kKeyName, s_name);
      wr.end();
    }
    return;
  }
  const String stored = pref.getString(kKeyName, s_name);
  pref.end();
  strncpy(s_name, stored.c_str(), kNameMax);
  s_name[kNameMax] = '\0';
  trim_inplace(s_name);
  if (s_name[0] == '\0') {
    strncpy(s_name, MYNAH_USER_NAME_DEFAULT, kNameMax);
    s_name[kNameMax] = '\0';
    save_name();
  }
}

const char *pm_user_display_name(void) {
  return s_name;
}

const char *pm_user_formal_name(void) {
  static char formal[kNameMax + 8];
  snprintf(formal, sizeof(formal), "Mr %s", s_name[0] ? s_name : MYNAH_USER_NAME_DEFAULT);
  return formal;
}

bool pm_user_set_name(const char *name) {
  char tmp[kNameMax + 1];
  if (!normalize_name(name, tmp, sizeof(tmp))) {
    return false;
  }
  strncpy(s_name, tmp, kNameMax);
  s_name[kNameMax] = '\0';
  save_name();
  return true;
}

bool pm_user_serial_command(const char *line) {
  if (!line) {
    return false;
  }
  if (strcmp(line, "name") == 0) {
    Serial.printf("name: %s (spoken as %s)\n", s_name[0] ? s_name : "(unset)", pm_user_formal_name());
    return true;
  }
  if (strncmp(line, "name ", 5) != 0) {
    return false;
  }
  const char *p = line + 5;
  while (*p == ' ') {
    ++p;
  }
  if (strncmp(p, "clear", 5) == 0 && (p[5] == '\0' || p[5] == ' ')) {
    if (pm_user_set_name(MYNAH_USER_NAME_DEFAULT)) {
      Serial.printf("name: reset to %s\n", s_name);
    }
    return true;
  }
  if (pm_user_set_name(p)) {
    Serial.printf("name: saved %s\n", s_name);
  } else {
    Serial.println("name: usage: name YourName   |   name clear");
  }
  return true;
}
