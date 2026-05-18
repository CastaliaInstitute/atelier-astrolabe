#include "pm_settings_http.h"

#include <WebServer.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "faces/pm_faces.h"
#include "pm_birth_nvs.h"
#include "pm_chart_profiles.h"
#include "pm_settings.h"
#include "pm_supabase_config.h"
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
    "a{color:#8cf}.msg{color:#8d8}.warn{color:#fc8}.card{border:1px solid #2a3040;"
    "border-radius:10px;padding:12px;margin:12px 0}ul{padding-left:18px}";

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
    const char c = in[i];
    const char *entity = nullptr;
    if (c == '"') {
      entity = "&quot;";
    } else if (c == '&') {
      entity = "&amp;";
    } else if (c == '<') {
      entity = "&lt;";
    } else if (c == '>') {
      entity = "&gt;";
    } else if (c == '\'') {
      entity = "&#39;";
    }
    if (entity) {
      const size_t n = strlen(entity);
      if (j + n >= out_cap) {
        break;
      }
      memcpy(out + j, entity, n);
      j += n;
    } else {
      out[j++] = c;
    }
  }
  out[j] = '\0';
}

static void append_html(char *buf, size_t cap, size_t *len, const char *chunk) {
  if (!buf || !cap || !len || !chunk || *len >= cap - 1) {
    return;
  }
  const size_t room = cap - *len - 1;
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
  char head[360];
  snprintf(head, sizeof(head),
           "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>%s</title><style>%s</style></head><body>",
           title, kCss);
  append_html(page, kPageCap, len, head);
  append_html(page, kPageCap, len,
              "<nav><a href=\"/settings\">Settings</a><a href=\"/settings/wifi\">WiFi</a>"
              "<a href=\"/settings/birth\">Birth</a><a href=\"/settings/family\">Synastry</a>"
              "<a href=\"/screen.bmp\">Screen</a></nav>");
}

static void page_end(char *page, size_t *len) {
  append_html(page, kPageCap, len, "</body></html>");
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
  const float lat = s_server->arg("lat").toFloat();
  const float lon = s_server->arg("lon").toFloat();
  if (y < 1900 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 ||
      mi > 59 || lat < -90.f || lat > 90.f || lon < -180.f || lon > 180.f) {
    return false;
  }
  memset(bb, 0, sizeof(*bb));
  bb->year = static_cast<uint16_t>(y);
  bb->month = static_cast<uint8_t>(mo);
  bb->day = static_cast<uint8_t>(d);
  bb->hour = static_cast<uint8_t>(h);
  bb->minute = static_cast<uint8_t>(mi);
  bb->lat_deg = lat;
  bb->lon_deg = lon;
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
  cp->role = (s_server->arg("role") == "child") ? PmChartRoleChild : PmChartRolePartner;
  cp->year = bb.year;
  cp->month = bb.month;
  cp->day = bb.day;
  cp->hour = bb.hour;
  cp->minute = bb.minute;
  cp->lat_deg = bb.lat_deg;
  cp->lon_deg = bb.lon_deg;
  cp->tz_offset_sec = bb.tz_offset_sec;
  strncpy(cp->place, bb.place, sizeof(cp->place) - 1);
  cp->place[sizeof(cp->place) - 1] = '\0';
  cp->valid = true;
  return true;
}

static void append_birth_fields(char *page, size_t *len, const PmBirthSpec *birth, bool have_birth) {
  char chunk[760];
  char place_esc[120];
  html_escape_attr(have_birth ? birth->place : "", place_esc, sizeof(place_esc));
  snprintf(chunk, sizeof(chunk),
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
           "<label>TZ offset (hours from UTC)<input name=\"tz_hours\" type=\"number\" step=\"0.5\" "
           "value=\"%.1f\"></label>",
           have_birth ? birth->year : 2000u, have_birth ? birth->month : 1u, have_birth ? birth->day : 1u,
           have_birth ? birth->hour : 12u, have_birth ? birth->minute : 0u, place_esc,
           have_birth ? birth->lat_deg : 0.f, have_birth ? birth->lon_deg : 0.f,
           have_birth ? (birth->tz_offset_sec / 3600.f) : 0.f);
  append_html(page, kPageCap, len, chunk);
}

static void append_birth_form(char *page, size_t *len, const PmBirthSpec *birth, bool have_birth,
                              const char *action) {
  char start[80];
  snprintf(start, sizeof(start), "<form method=\"POST\" action=\"%s\">", action);
  append_html(page, kPageCap, len, start);
  append_birth_fields(page, len, birth, have_birth);
  append_html(page, kPageCap, len, "<button type=\"submit\">Save</button></form>");
}

static void append_face_select(char *page, size_t *len, ClockFace selected) {
  append_html(page, kPageCap, len, "<label>Default face<select name=\"face\">");
  for (int i = 0; i < static_cast<int>(ClockFace::kNumFaces); ++i) {
    const ClockFace face = static_cast<ClockFace>(i);
    char opt[120];
    snprintf(opt, sizeof(opt), "<option value=\"%d\" %s>%d - %s</option>", i,
             face == selected ? "selected" : "", i, pm_faces_name(face));
    append_html(page, kPageCap, len, opt);
  }
  append_html(page, kPageCap, len, "</select></label>");
}

static void handle_settings_hub() {
  char *page = alloc_page();
  if (!page) {
    s_server->send(500, "text/plain", "alloc failed");
    return;
  }
  pm_settings_refresh_url();
  size_t len = 0;
  page_begin(page, &len, "Astrolabe settings");
  append_html(page, kPageCap, &len, "<h1>Astrolabe settings</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Saved to watch NVS.</p>");
  }
  if (s_server->hasArg("wifi")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">WiFi saved. Reconnecting with NVS credentials.</p>");
  }
  char line[220];
  snprintf(line, sizeof(line), "<p>LAN config: <a href=\"%s\"><code>%s</code></a></p>", pm_settings_url(),
           pm_settings_host_label());
  append_html(page, kPageCap, &len, line);
  append_html(page, kPageCap, &len,
              "<ul><li><a href=\"/settings/wifi\">WiFi network</a></li>"
              "<li><a href=\"/settings/birth\">Birth chart</a></li>"
              "<li><a href=\"/settings/family\">Synastry profiles</a></li></ul>");

  append_html(page, kPageCap, &len, "<h2>Default face</h2><form method=\"POST\" action=\"/settings/default\">");
  append_face_select(page, &len, pm_faces_default_load());
  append_html(page, kPageCap, &len, "<button type=\"submit\">Save default</button></form>");

  char url[160] = "";
  char override_url[160] = "";
  char url_esc[220];
  const bool has_override = pm_supabase_url_override_load(override_url, sizeof(override_url));
  (void)pm_supabase_url_get(url, sizeof(url));
  html_escape_attr(has_override ? override_url : "", url_esc, sizeof(url_esc));
  append_html(page, kPageCap, &len, "<h2>Supabase URL override</h2>");
  snprintf(line, sizeof(line), "<p>Effective URL: <code>%s</code></p>", url[0] ? url : "(not configured)");
  append_html(page, kPageCap, &len, line);
  append_html(page, kPageCap, &len, "<p class=\"warn\">Dev override only. The anon key remains build-time only.</p>");
  char supabase_form[720];
  snprintf(supabase_form, sizeof(supabase_form),
           "<form method=\"POST\" action=\"/settings/supabase\">"
           "<label>Override URL<input name=\"url\" maxlength=\"159\" value=\"%s\" "
           "placeholder=\"https://project.supabase.co\"></label>"
           "<button type=\"submit\">Save override</button></form>"
           "<form method=\"POST\" action=\"/settings/supabase\" style=\"margin-top:8px\">"
           "<input type=\"hidden\" name=\"url\" value=\"\"><button type=\"submit\" class=\"danger\">Clear override</button>"
           "</form>",
           url_esc);
  append_html(page, kPageCap, &len, supabase_form);
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
  char ssid_esc[96];
  html_escape_attr(ssid, ssid_esc, sizeof(ssid_esc));

  size_t len = 0;
  page_begin(page, &len, "WiFi");
  append_html(page, kPageCap, &len, "<h1>WiFi</h1>");
  if (s_server->hasArg("fail")) {
    append_html(page, kPageCap, &len, "<p class=\"warn\">Could not save. SSID is required.</p>");
  }
  char chunk[520];
  snprintf(chunk, sizeof(chunk),
           "<form method=\"POST\" action=\"/settings/wifi\">"
           "<label>SSID<input name=\"ssid\" maxlength=\"32\" value=\"%s\" required></label>"
           "<label>Password<input name=\"password\" type=\"password\" maxlength=\"64\" "
           "placeholder=\"leave blank to keep current\"></label>"
           "<button type=\"submit\">Save and reconnect</button></form>",
           ssid_esc);
  append_html(page, kPageCap, &len, chunk);
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
  page_begin(page, &len, "Birth");
  append_html(page, kPageCap, &len, "<h1>Birth chart</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Birth saved.</p>");
  }
  if (s_server->hasArg("cleared")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Birth data cleared.</p>");
  }
  append_birth_form(page, &len, &birth, have_birth, "/settings/birth");
  append_html(page, kPageCap, &len,
              "<form method=\"POST\" action=\"/settings/birth/clear\" style=\"margin-top:20px\">"
              "<button type=\"submit\" class=\"danger\">Clear birth</button></form>");
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
  page_begin(page, &len, "Synastry profiles");
  append_html(page, kPageCap, &len, "<h1>Synastry profiles</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Profile saved.</p>");
  }
  if (s_server->hasArg("active")) {
    append_html(page, kPageCap, &len, "<p class=\"msg\">Active target changed.</p>");
  }
  const int active = pm_chart_profiles_active_slot();
  bool any = false;
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    PmChartProfile p = {};
    if (!pm_chart_profile_get(i, &p)) {
      continue;
    }
    any = true;
    char name_esc[64];
    char place_esc[96];
    html_escape_attr(p.name, name_esc, sizeof(name_esc));
    html_escape_attr(p.place, place_esc, sizeof(place_esc));
    char card[520];
    snprintf(card, sizeof(card),
             "<div class=\"card\"><strong>%s%s</strong> (%s)<br>%04u-%02u-%02u %02u:%02u<br>%s"
             "<br><a href=\"/settings/family/edit?slot=%d\">Edit</a>"
             "<form method=\"POST\" action=\"/settings/family/active\" style=\"display:inline;margin-left:10px\">"
             "<input type=\"hidden\" name=\"slot\" value=\"%d\"><button type=\"submit\">Use</button></form></div>",
             i == active ? "* " : "", name_esc, pm_chart_role_label(p.role), p.year, p.month, p.day, p.hour,
             p.minute, place_esc, i, i);
    append_html(page, kPageCap, &len, card);
  }
  if (!any) {
    append_html(page, kPageCap, &len, "<p>No saved profiles yet.</p>");
  }
  const int free_slot = pm_chart_profile_first_free_slot();
  if (free_slot >= 0) {
    char link[100];
    snprintf(link, sizeof(link), "<p><a href=\"/settings/family/edit?slot=%d\">Add profile</a></p>", free_slot);
    append_html(page, kPageCap, &len, link);
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
    birth.place[sizeof(birth.place) - 1] = '\0';
    birth.valid = true;
  }

  size_t len = 0;
  page_begin(page, &len, "Edit profile");
  append_html(page, kPageCap, &len, "<h1>Synastry profile</h1>");
  char name_esc[64];
  html_escape_attr(have ? p.name : "", name_esc, sizeof(name_esc));
  const char *role_partner = (!have || p.role == PmChartRolePartner) ? "selected" : "";
  const char *role_child = (have && p.role == PmChartRoleChild) ? "selected" : "";
  char chunk[420];
  snprintf(chunk, sizeof(chunk),
           "<form method=\"POST\" action=\"/settings/family\">"
           "<input type=\"hidden\" name=\"slot\" value=\"%d\">"
           "<label>Name<input name=\"name\" maxlength=\"23\" value=\"%s\" required></label>"
           "<label>Role<select name=\"role\"><option value=\"partner\" %s>Partner</option>"
           "<option value=\"child\" %s>Child</option></select></label>",
           slot, name_esc, role_partner, role_child);
  append_html(page, kPageCap, &len, chunk);
  append_birth_fields(page, &len, &birth, have);
  append_html(page, kPageCap, &len, "<button type=\"submit\">Save profile</button></form>");
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

static void handle_wifi_post() {
  String ssid = s_server->arg("ssid");
  ssid.trim();
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
    pm_settings_note_wifi_changed();
    pm_wifi_request_reconnect();
    redirect("/settings?wifi=1");
  } else {
    redirect("/settings/wifi?fail=1");
  }
}

static void handle_birth_post() {
  PmBirthSpec bb = {};
  if (parse_birth_args(&bb)) {
    pm_birth_save(&bb);
  }
  redirect("/settings/birth?saved=1");
}

static void handle_birth_clear_post() {
  pm_birth_clear();
  redirect("/settings/birth?cleared=1");
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

static void handle_family_active_post() {
  if (s_server->hasArg("slot")) {
    (void)pm_chart_profiles_set_active_slot(s_server->arg("slot").toInt());
  }
  redirect("/settings/family?active=1");
}

static void handle_default_post() {
  ClockFace face = ClockFace::ClassicAnalog;
  if (pm_faces_from_index(s_server->arg("face").toInt(), &face)) {
    (void)pm_faces_default_save(face);
    pm_faces_set(face);
  }
  redirect("/settings?saved=1");
}

static void handle_supabase_post() {
  const String url = s_server->arg("url");
  if (url.length() == 0) {
    pm_supabase_url_override_clear();
  } else {
    (void)pm_supabase_url_override_save(url.c_str());
  }
  redirect("/settings?saved=1");
}

void pm_settings_http_register(WebServer *server) {
  if (!server) {
    return;
  }
  s_server = server;
  server->on("/settings", HTTP_GET, handle_settings_hub);
  server->on("/settings/wifi", HTTP_GET, handle_wifi_get);
  server->on("/settings/wifi", HTTP_POST, handle_wifi_post);
  server->on("/settings/birth", HTTP_GET, handle_birth_get);
  server->on("/settings/birth", HTTP_POST, handle_birth_post);
  server->on("/settings/birth/clear", HTTP_POST, handle_birth_clear_post);
  server->on("/settings/family", HTTP_GET, handle_family_get);
  server->on("/settings/family", HTTP_POST, handle_family_post);
  server->on("/settings/family/edit", HTTP_GET, handle_family_edit_get);
  server->on("/settings/family/delete", HTTP_POST, handle_family_delete_post);
  server->on("/settings/family/active", HTTP_POST, handle_family_active_post);
  server->on("/settings/default", HTTP_POST, handle_default_post);
  server->on("/settings/supabase", HTTP_POST, handle_supabase_post);
}
