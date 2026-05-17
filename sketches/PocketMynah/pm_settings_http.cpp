#include "pm_settings_http.h"

#include <WebServer.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "pm_birth_nvs.h"
#include "pm_chart_profiles.h"
#include "pm_cycle_nvs.h"
#include "pm_settings.h"
#include "pm_wifi_creds.h"
#include "pm_wifi_ntp.h"

static WebServer *s_server = nullptr;

static constexpr size_t kPageCap = 16384;

static const char kCss[] =
    "body{margin:0;padding:16px;background:#0e1018;color:#d8dce8;font-family:system-ui,sans-serif}"
    "h1,h2{font-size:1.15rem;margin:1.2rem 0 .5rem}nav a{margin-right:12px}"
    "label{display:block;margin:10px 0 4px;font-size:.85rem;color:#9aa3b8}"
    "input,select,button{font-size:1rem;padding:8px;border-radius:8px;border:1px solid #333;"
    "background:#1a1f2e;color:#eee;width:100%;box-sizing:border-box}"
    "button{background:#3d5a80;margin-top:12px;width:auto}button.danger{background:#6a3030}"
    ".row{display:flex;gap:8px;flex-wrap:wrap}.row label{flex:1 1 45%}"
    "a{color:#8cf}.msg{color:#8d8}.card{border:1px solid #2a3040;border-radius:10px;padding:12px;margin:12px 0}"
    "ul{padding-left:18px}";

static char *alloc_page(void) {
  char *page = static_cast<char *>(heap_caps_malloc(kPageCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!page) {
    page = static_cast<char *>(malloc(kPageCap));
  }
  return page;
}

static void html_escape_attr(const char *in, char *out, size_t out_cap) {
  if (!out || out_cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!in) {
    return;
  }
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < out_cap; ++i) {
    if (in[i] == '\"' || in[i] == '&' || in[i] == '<') {
      if (j + 6 >= out_cap) {
        break;
      }
      if (in[i] == '\"') {
        memcpy(out + j, "&quot;", 6);
        j += 6;
      } else if (in[i] == '&') {
        memcpy(out + j, "&amp;", 5);
        j += 5;
      } else {
        memcpy(out + j, "&lt;", 4);
        j += 4;
      }
    } else {
      out[j++] = in[i];
    }
  }
  out[j] = '\0';
}

static void append_html(char *buf, size_t cap, size_t *len, const char *chunk) {
  if (!buf || !cap || !len || !chunk) {
    return;
  }
  const size_t room = (*len < cap) ? (cap - *len - 1) : 0;
  if (room == 0) {
    return;
  }
  const int n = snprintf(buf + *len, room + 1, "%s", chunk);
  if (n > 0) {
    *len += static_cast<size_t>(n);
    if (*len >= cap) {
      *len = cap - 1;
    }
  }
}

static void page_begin(char *page, size_t *len, const char *title) {
  *len = 0;
  char head[320];
  snprintf(head, sizeof(head),
           "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>%s</title><style>%s</style></head><body>",
           title, kCss);
  append_html(page, kPageCap, len, head);
  append_html(page, kPageCap, len,
              "<nav><a href=\"/settings\">Settings</a><a href=\"/settings/wifi\">WiFi</a>"
              "<a href=\"/settings/birth\">Your birth</a><a href=\"/settings/family\">Family</a>"
              "<a href=\"/settings/cycle\">Cycle</a></nav>");
}

static void page_end(char *page, size_t *len) {
  append_html(page, kPageCap, len, "<p style=\"margin-top:24px;font-size:.85rem\">"
                                  "<a href=\"/screen.bmp\">Screen capture</a></p></body></html>");
}

static void send_page(int code, char *page) {
  if (!s_server || !page) {
    free(page);
    return;
  }
  s_server->send(code, "text/html", page);
  free(page);
}

static void redirect(const char *path) {
  if (!s_server) {
    return;
  }
  s_server->sendHeader("Location", path, true);
  s_server->send(303, "text/plain", "");
}

static bool parse_birth_args(PmBirthSpec *bb) {
  if (!s_server || !bb) {
    return false;
  }
  const int y = s_server->arg("year").toInt();
  const int mo = s_server->arg("month").toInt();
  const int d = s_server->arg("day").toInt();
  const int h = s_server->arg("hour").toInt();
  const int mi = s_server->arg("minute").toInt();
  if (y < 1900 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) {
    return false;
  }
  memset(bb, 0, sizeof(*bb));
  bb->year = static_cast<uint16_t>(y);
  bb->month = static_cast<uint8_t>(mo);
  bb->day = static_cast<uint8_t>(d);
  bb->hour = static_cast<uint8_t>(h);
  bb->minute = static_cast<uint8_t>(mi);
  bb->lat_deg = s_server->arg("lat").toFloat();
  bb->lon_deg = s_server->arg("lon").toFloat();
  bb->tz_offset_sec = static_cast<int32_t>(s_server->arg("tz_hours").toFloat() * 3600.f);
  const String place = s_server->arg("place");
  strncpy(bb->place, place.c_str(), sizeof(bb->place) - 1);
  bb->place[sizeof(bb->place) - 1] = '\0';
  bb->valid = true;
  return true;
}

static bool parse_chart_args(PmChartProfile *cp) {
  if (!s_server || !cp) {
    return false;
  }
  const String name = s_server->arg("name");
  if (name.length() == 0) {
    return false;
  }
  PmBirthSpec bb = {};
  if (!parse_birth_args(&bb)) {
    return false;
  }
  memset(cp, 0, sizeof(*cp));
  strncpy(cp->name, name.c_str(), sizeof(cp->name) - 1);
  cp->name[sizeof(cp->name) - 1] = '\0';
  const String role = s_server->arg("role");
  cp->role = (role == "child") ? PmChartRoleChild : PmChartRolePartner;
  cp->year = bb.year;
  cp->month = bb.month;
  cp->day = bb.day;
  cp->hour = bb.hour;
  cp->minute = bb.minute;
  cp->lat_deg = bb.lat_deg;
  cp->lon_deg = bb.lon_deg;
  cp->tz_offset_sec = bb.tz_offset_sec;
  strncpy(cp->place, bb.place, sizeof(cp->place) - 1);
  cp->valid = true;
  return true;
}

static void append_birth_form(char *page, size_t *len, const PmBirthSpec *birth, const bool have_birth,
                              const char *action) {
  char chunk[640];
  char place_esc[120];
  html_escape_attr(have_birth ? birth->place : "", place_esc, sizeof(place_esc));
  snprintf(chunk, sizeof(chunk),
           "<form method=\"POST\" action=\"%s\"><div class=\"row\">"
           "<label>Year<input name=\"year\" type=\"number\" min=\"1900\" max=\"2100\" value=\"%u\"></label>"
           "<label>Month<input name=\"month\" type=\"number\" min=\"1\" max=\"12\" value=\"%u\"></label>"
           "<label>Day<input name=\"day\" type=\"number\" min=\"1\" max=\"31\" value=\"%u\"></label>"
           "</div><div class=\"row\">"
           "<label>Hour<input name=\"hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%u\"></label>"
           "<label>Minute<input name=\"minute\" type=\"number\" min=\"0\" max=\"59\" value=\"%u\"></label>"
           "</div>"
           "<label>Place<input name=\"place\" maxlength=\"39\" value=\"%s\"></label>"
           "<label>Latitude<input name=\"lat\" type=\"number\" step=\"0.0001\" value=\"%.4f\"></label>"
           "<label>Longitude<input name=\"lon\" type=\"number\" step=\"0.0001\" value=\"%.4f\"></label>"
           "<label>TZ offset (hours from UTC)<input name=\"tz_hours\" type=\"number\" step=\"0.5\" value=\"%.1f\">"
           "</label><button type=\"submit\">Save</button></form>",
           action, have_birth ? birth->year : 2000u, have_birth ? birth->month : 1u, have_birth ? birth->day : 1u,
           have_birth ? birth->hour : 12u, have_birth ? birth->minute : 0u, place_esc,
           have_birth ? birth->lat_deg : 0.f, have_birth ? birth->lon_deg : 0.f,
           have_birth ? (birth->tz_offset_sec / 3600.f) : 0.f);
  append_html(page, kPageCap, len, chunk);
}

static void handle_settings_hub() {
  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }
  size_t len = 0;
  page_begin(page, &len, "Astrolabe settings");
  append_html(page, kPageCap, &len, "<h1>Astrolabe settings</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Saved to watch NVS.</p>");
  }
  if (s_server->hasArg("wifi")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">WiFi saved — reconnecting…</p>");
  }
  char line[160];
  snprintf(line, sizeof(line), "<p>LAN host: <code>%s</code></p>", pm_settings_host_label());
  append_html(page, kPageCap, &len, line);
  append_html(page, kPageCap, &len,
              "<ul><li><a href=\"/settings/wifi\">WiFi network &amp; password</a></li>"
              "<li><a href=\"/settings/birth\">Your birth chart</a></li>"
              "<li><a href=\"/settings/family\">Partners &amp; children</a></li>"
              "<li><a href=\"/settings/cycle\">Menstrual cycle &amp; pregnancy</a></li></ul>");
  page_end(page, &len);
  send_page(200, page);
}

static void handle_birth_get() {
  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }
  PmBirthSpec birth = {};
  const bool have_birth = pm_birth_load(&birth);
  size_t len = 0;
  page_begin(page, &len, "Your birth");
  append_html(page, kPageCap, &len, "<h1>Your birth chart</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Saved to watch NVS.</p>");
  }
  if (s_server->hasArg("cleared")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Birth data cleared.</p>");
  }
  append_birth_form(page, &len, &birth, have_birth, "/settings/birth");
  append_html(page, kPageCap, &len,
              "<form method=\"POST\" action=\"/settings/clear\" style=\"margin-top:20px\">"
              "<button type=\"submit\" class=\"danger\">Clear birth</button></form>");
  page_end(page, &len);
  send_page(200, page);
}

static void handle_wifi_get() {
  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }
  char ssid[64] = "";
  char pass[64] = "";
  (void)pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass));

  size_t len = 0;
  page_begin(page, &len, "WiFi");
  append_html(page, kPageCap, &len, "<h1>WiFi</h1>");
  if (s_server->hasArg("fail")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Could not save — SSID required.</p>");
  }

  append_html(page, kPageCap, &len, "<form method=\"POST\" action=\"/settings/wifi\">");
  append_html(page, kPageCap, &len, "<label>Network<select name=\"ssid_select\">");
  char opt[140];
  char esc[80];
  html_escape_attr(ssid, esc, sizeof(esc));
  snprintf(opt, sizeof(opt), "<option value=\"%s\" selected>Current: %s</option>", esc, esc);
  append_html(page, kPageCap, &len, opt);
  append_html(page, kPageCap, &len, "<option value=\"\">— scan —</option>");

  const int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n && i < 24; ++i) {
    const String s = WiFi.SSID(i);
    if (s.length() == 0) {
      continue;
    }
    html_escape_attr(s.c_str(), esc, sizeof(esc));
    snprintf(opt, sizeof(opt), "<option value=\"%s\">%s (%d dBm)</option>", esc, esc, WiFi.RSSI(i));
    append_html(page, kPageCap, &len, opt);
  }
  WiFi.scanDelete();
  append_html(page, kPageCap, &len, "</select></label>");

  html_escape_attr(ssid, esc, sizeof(esc));
  snprintf(opt, sizeof(opt), "<label>Or SSID<input name=\"ssid_manual\" maxlength=\"32\" value=\"%s\"></label>", esc);
  append_html(page, kPageCap, &len, opt);
  append_html(page, kPageCap, &len,
              "<label>Password<input name=\"password\" type=\"password\" maxlength=\"64\" "
              "placeholder=\"leave blank to keep current\"></label>"
              "<button type=\"submit\">Save &amp; reconnect</button></form>");
  page_end(page, &len);
  send_page(200, page);
}

static void handle_family_get() {
  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }
  size_t len = 0;
  page_begin(page, &len, "Family");
  append_html(page, kPageCap, &len, "<h1>Partners &amp; children</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Profile saved.</p>");
  }
  append_html(page, kPageCap, &len, "<p>Up to 8 profiles stored on the watch for synastry charts.</p>");

  bool any = false;
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    PmChartProfile p = {};
    if (!pm_chart_profile_get(i, &p)) {
      continue;
    }
    any = true;
    char card[280];
    char name_esc[48];
    html_escape_attr(p.name, name_esc, sizeof(name_esc));
    snprintf(card, sizeof(card),
             "<div class=\"card\"><strong>%s</strong> (%s)<br>%04u-%02u-%02u %02u:%02u · %s"
             "<br><a href=\"/settings/family/edit?slot=%d\">Edit</a></div>",
             name_esc, pm_chart_role_label(p.role), p.year, p.month, p.day, p.hour, p.minute, p.place, i);
    append_html(page, kPageCap, &len, card);
  }
  if (!any) {
    append_html(page, kPageCap, &len, "<p>No family profiles yet.</p>");
  }
  const int free_slot = pm_chart_profile_first_free_slot();
  if (free_slot >= 0) {
    char link[96];
    snprintf(link, sizeof(link), "<p><a href=\"/settings/family/edit?slot=%d\">Add profile</a></p>", free_slot);
    append_html(page, kPageCap, &len, link);
  } else {
    append_html(page, kPageCap, &len, "<p>All slots full — edit or delete a profile.</p>");
  }
  page_end(page, &len);
  send_page(200, page);
}

static void handle_family_edit_get() {
  if (!s_server->hasArg("slot")) {
    redirect("/settings/family");
    return;
  }
  const int slot = s_server->arg("slot").toInt();
  if (slot < 0 || slot >= kPmChartProfileSlots) {
    redirect("/settings/family");
    return;
  }

  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }

  PmChartProfile p = {};
  const bool have = pm_chart_profile_get(slot, &p);
  PmBirthSpec birth = {};
  if (have) {
    birth.year = p.year;
    birth.month = p.month;
    birth.day = p.day;
    birth.hour = p.hour;
    birth.minute = p.minute;
    birth.lat_deg = p.lat_deg;
    birth.lon_deg = p.lon_deg;
    birth.tz_offset_sec = p.tz_offset_sec;
    strncpy(birth.place, p.place, sizeof(birth.place) - 1);
    birth.valid = true;
  }

  size_t len = 0;
  page_begin(page, &len, "Edit profile");
  append_html(page, kPageCap, &len, "<h1>Family profile</h1>");

  char chunk[400];
  char name_esc[48];
  html_escape_attr(have ? p.name : "", name_esc, sizeof(name_esc));
  const char *role_partner = (!have || p.role == PmChartRolePartner) ? "selected" : "";
  const char *role_child = (have && p.role == PmChartRoleChild) ? "selected" : "";
  snprintf(chunk, sizeof(chunk),
           "<form method=\"POST\" action=\"/settings/family\">"
           "<input type=\"hidden\" name=\"slot\" value=\"%d\">"
           "<label>Name<input name=\"name\" maxlength=\"23\" value=\"%s\" required></label>"
           "<label>Role<select name=\"role\">"
           "<option value=\"partner\" %s>Partner</option>"
           "<option value=\"child\" %s>Child</option></select></label>",
           slot, name_esc, role_partner, role_child);
  append_html(page, kPageCap, &len, chunk);
  {
    char chunk2[640];
    char place_esc[120];
    html_escape_attr(have ? birth.place : "", place_esc, sizeof(place_esc));
    snprintf(chunk2, sizeof(chunk2),
             "<div class=\"row\">"
             "<label>Year<input name=\"year\" type=\"number\" min=\"1900\" max=\"2100\" value=\"%u\"></label>"
             "<label>Month<input name=\"month\" type=\"number\" min=\"1\" max=\"12\" value=\"%u\"></label>"
             "<label>Day<input name=\"day\" type=\"number\" min=\"1\" max=\"31\" value=\"%u\"></label>"
             "</div><div class=\"row\">"
             "<label>Hour<input name=\"hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%u\"></label>"
             "<label>Minute<input name=\"minute\" type=\"number\" min=\"0\" max=\"59\" value=\"%u\"></label>"
             "</div>"
             "<label>Place<input name=\"place\" maxlength=\"39\" value=\"%s\"></label>"
             "<label>Latitude<input name=\"lat\" type=\"number\" step=\"0.0001\" value=\"%.4f\"></label>"
             "<label>Longitude<input name=\"lon\" type=\"number\" step=\"0.0001\" value=\"%.4f\"></label>"
             "<label>TZ offset (hours from UTC)<input name=\"tz_hours\" type=\"number\" step=\"0.5\" value=\"%.1f\">"
             "</label><button type=\"submit\">Save profile</button></form>",
             have ? birth.year : 2000u, have ? birth.month : 1u, have ? birth.day : 1u, have ? birth.hour : 12u,
             have ? birth.minute : 0u, place_esc, have ? birth.lat_deg : 0.f, have ? birth.lon_deg : 0.f,
             have ? (birth.tz_offset_sec / 3600.f) : 0.f);
    append_html(page, kPageCap, &len, chunk2);
  }
  if (have) {
    snprintf(chunk, sizeof(chunk),
             "<form method=\"POST\" action=\"/settings/family/delete\" style=\"margin-top:16px\">"
             "<input type=\"hidden\" name=\"slot\" value=\"%d\">"
             "<button type=\"submit\" class=\"danger\">Delete profile</button></form>",
             slot);
    append_html(page, kPageCap, &len, chunk);
  }
  page_end(page, &len);
  send_page(200, page);
}

static void handle_settings_post_birth() {
  PmBirthSpec bb = {};
  if (parse_birth_args(&bb)) {
    pm_birth_save(&bb);
  }
  redirect("/settings/birth?saved=1");
}

static void handle_settings_clear() {
  pm_birth_clear();
  redirect("/settings/birth?cleared=1");
}

static void handle_wifi_post() {
  String ssid = s_server->arg("ssid_select");
  if (ssid.length() == 0) {
    ssid = s_server->arg("ssid_manual");
  }
  const String pass_new = s_server->arg("password");
  if (ssid.length() == 0) {
    redirect("/settings/wifi?fail=1");
    return;
  }
  char pass[64] = "";
  if (pass_new.length() > 0) {
    strncpy(pass, pass_new.c_str(), sizeof(pass) - 1);
    pass[sizeof(pass) - 1] = '\0';
  } else {
    char old_ssid[64] = "";
    (void)pm_wifi_credentials_load(old_ssid, sizeof(old_ssid), pass, sizeof(pass));
  }
  if (pm_wifi_credentials_save(ssid.c_str(), pass)) {
    pm_wifi_request_reconnect();
    pm_settings_refresh_url();
    redirect("/settings?wifi=1");
  } else {
    redirect("/settings/wifi?fail=1");
  }
}

static void handle_family_post() {
  if (!s_server->hasArg("slot")) {
    redirect("/settings/family");
    return;
  }
  const int slot = s_server->arg("slot").toInt();
  PmChartProfile cp = {};
  if (slot >= 0 && slot < kPmChartProfileSlots && parse_chart_args(&cp)) {
    (void)pm_chart_profile_save(slot, &cp);
  }
  redirect("/settings/family?saved=1");
}

static void handle_family_delete_post() {
  if (s_server->hasArg("slot")) {
    pm_chart_profile_clear(s_server->arg("slot").toInt());
  }
  redirect("/settings/family");
}

static void handle_cycle_get() {
  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }
  PmCycleProfile c = {};
  (void)pm_cycle_load(&c);

  size_t len = 0;
  page_begin(page, &len, "Cycle");
  append_html(page, kPageCap, &len, "<h1>Menstrual cycle &amp; pregnancy</h1>");
  append_html(page, kPageCap, &len,
              "<p class=\"msg\">Wellness estimate only; stored on watch (NVS). Not medical advice.</p>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Saved.</p>");
  }

  if (c.pregnancy_active && c.has_due_date && pm_time_valid()) {
    struct tm loc = {};
    pm_time_local(&loc);
    const uint16_t y = static_cast<uint16_t>(loc.tm_year + 1900);
    const uint8_t mo = static_cast<uint8_t>(loc.tm_mon + 1);
    const uint8_t d = static_cast<uint8_t>(loc.tm_mday);
    const int32_t gest = pm_cycle_gestational_day(&c, y, mo, d);
    const int32_t until = pm_cycle_days_until_due(&c, y, mo, d);
    char status[120];
    if (gest >= 0 && until != INT32_MIN) {
      snprintf(status, sizeof(status),
               "<p>Now: gestational week %ld · due %04u-%02u-%02u (%ld days)</p>",
               static_cast<long>((gest + 3) / 7), c.due_year, c.due_month, c.due_day, static_cast<long>(until));
    } else {
      snprintf(status, sizeof(status), "<p>Due date: %04u-%02u-%02u</p>", c.due_year, c.due_month, c.due_day);
    }
    append_html(page, kPageCap, &len, status);
  } else if (c.has_last_period && pm_time_valid()) {
    struct tm loc = {};
    pm_time_local(&loc);
    const int32_t idx = pm_cycle_day_index_for_date(
        &c, static_cast<uint16_t>(loc.tm_year + 1900), static_cast<uint8_t>(loc.tm_mon + 1),
        static_cast<uint8_t>(loc.tm_mday));
    if (idx >= 0) {
      char status[80];
      snprintf(status, sizeof(status), "<p>Cycle day %ld of %u today.</p>", static_cast<long>(idx + 1),
               c.cycle_length_days);
      append_html(page, kPageCap, &len, status);
    }
  }

  char chunk[900];
  const char *preg_chk = c.pregnancy_active ? "checked" : "";
  snprintf(chunk, sizeof(chunk),
           "<form method=\"POST\" action=\"/settings/cycle\">"
           "<h2>Pregnancy</h2>"
           "<label><input type=\"checkbox\" name=\"pregnant\" value=\"1\" %s> Tracking pregnancy</label>"
           "<div class=\"row\">"
           "<label>Due date — year<input name=\"due_year\" type=\"number\" min=\"2000\" max=\"2100\" value=\"%u\"></label>"
           "<label>Month<input name=\"due_month\" type=\"number\" min=\"1\" max=\"12\" value=\"%u\"></label>"
           "<label>Day<input name=\"due_day\" type=\"number\" min=\"1\" max=\"31\" value=\"%u\"></label>"
           "</div>"
           "<h2>Menstrual cycle</h2>"
           "<div class=\"row\">"
           "<label>Last period — year<input name=\"lp_year\" type=\"number\" min=\"2000\" max=\"2100\" value=\"%u\"></label>"
           "<label>Month<input name=\"lp_month\" type=\"number\" min=\"1\" max=\"12\" value=\"%u\"></label>"
           "<label>Day<input name=\"lp_day\" type=\"number\" min=\"1\" max=\"31\" value=\"%u\"></label>"
           "</div>"
           "<div class=\"row\">"
           "<label>Cycle length (days)<input name=\"cycle_len\" type=\"number\" min=\"%u\" max=\"%u\" value=\"%u\"></label>"
           "<label>Period length (days)<input name=\"period_len\" type=\"number\" min=\"1\" max=\"10\" value=\"%u\"></label>"
           "</div>"
           "<button type=\"submit\">Save</button></form>",
           preg_chk, c.has_due_date ? c.due_year : 2026u, c.has_due_date ? c.due_month : 1u,
           c.has_due_date ? c.due_day : 1u, c.has_last_period ? c.last_period_year : 2000u,
           c.has_last_period ? c.last_period_month : 1u, c.has_last_period ? c.last_period_day : 1u,
           PM_CYCLE_MIN_LENGTH_DAYS, PM_CYCLE_MAX_LENGTH_DAYS, c.cycle_length_days, c.period_length_days);
  append_html(page, kPageCap, &len, chunk);

  append_html(page, kPageCap, &len,
              "<form method=\"POST\" action=\"/settings/cycle/today\" style=\"margin-top:16px\">"
              "<button type=\"submit\">Log period started today</button></form>"
              "<form method=\"POST\" action=\"/settings/cycle/clear\" style=\"margin-top:12px\">"
              "<button type=\"submit\" class=\"danger\">Clear all cycle data</button></form>");
  page_end(page, &len);
  send_page(200, page);
}

static void handle_cycle_post() {
  PmCycleProfile p = {};
  (void)pm_cycle_load(&p);

  p.pregnancy_active = s_server->hasArg("pregnant");
  const int due_y = s_server->arg("due_year").toInt();
  const int due_mo = s_server->arg("due_month").toInt();
  const int due_d = s_server->arg("due_day").toInt();
  if (p.pregnancy_active && pm_cycle_ymd_sane(static_cast<uint16_t>(due_y), static_cast<uint8_t>(due_mo),
                                              static_cast<uint8_t>(due_d))) {
    p.due_year = static_cast<uint16_t>(due_y);
    p.due_month = static_cast<uint8_t>(due_mo);
    p.due_day = static_cast<uint8_t>(due_d);
    p.has_due_date = true;
  } else {
    p.pregnancy_active = false;
    p.has_due_date = false;
  }

  const int lp_y = s_server->arg("lp_year").toInt();
  const int lp_mo = s_server->arg("lp_month").toInt();
  const int lp_d = s_server->arg("lp_day").toInt();
  if (pm_cycle_ymd_sane(static_cast<uint16_t>(lp_y), static_cast<uint8_t>(lp_mo), static_cast<uint8_t>(lp_d))) {
    p.last_period_year = static_cast<uint16_t>(lp_y);
    p.last_period_month = static_cast<uint8_t>(lp_mo);
    p.last_period_day = static_cast<uint8_t>(lp_d);
    p.has_last_period = true;
  } else {
    p.has_last_period = false;
  }

  const int clen = s_server->arg("cycle_len").toInt();
  const int plen = s_server->arg("period_len").toInt();
  if (clen >= PM_CYCLE_MIN_LENGTH_DAYS && clen <= PM_CYCLE_MAX_LENGTH_DAYS) {
    p.cycle_length_days = static_cast<uint8_t>(clen);
  }
  if (plen >= 1 && plen < p.cycle_length_days) {
    p.period_length_days = static_cast<uint8_t>(plen);
  }

  pm_cycle_save(&p);
  redirect("/settings/cycle?saved=1");
}

static void handle_cycle_today_post() {
  struct tm loc = {};
  if (pm_time_valid()) {
    pm_time_local(&loc);
    (void)pm_cycle_log_period_started_today(&loc);
  }
  redirect("/settings/cycle?saved=1");
}

static void handle_cycle_clear_post() {
  pm_cycle_clear();
  redirect("/settings/cycle?saved=1");
}

void pm_settings_http_register(WebServer *server) {
  if (!server) {
    return;
  }
  s_server = server;
  server->on("/settings", HTTP_GET, handle_settings_hub);
  server->on("/settings/birth", HTTP_GET, handle_birth_get);
  server->on("/settings/birth", HTTP_POST, handle_settings_post_birth);
  server->on("/settings/clear", HTTP_POST, handle_settings_clear);
  server->on("/settings/wifi", HTTP_GET, handle_wifi_get);
  server->on("/settings/wifi", HTTP_POST, handle_wifi_post);
  server->on("/settings/family", HTTP_GET, handle_family_get);
  server->on("/settings/family", HTTP_POST, handle_family_post);
  server->on("/settings/family/edit", HTTP_GET, handle_family_edit_get);
  server->on("/settings/family/delete", HTTP_POST, handle_family_delete_post);
  server->on("/settings/cycle", HTTP_GET, handle_cycle_get);
  server->on("/settings/cycle", HTTP_POST, handle_cycle_post);
  server->on("/settings/cycle/today", HTTP_POST, handle_cycle_today_post);
  server->on("/settings/cycle/clear", HTTP_POST, handle_cycle_clear_post);
}
