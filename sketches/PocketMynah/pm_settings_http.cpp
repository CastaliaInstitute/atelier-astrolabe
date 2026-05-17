#include "pm_settings_http.h"

#include <WebServer.h>
#include <cstdio>
#include <cstring>

#include "pm_birth_nvs.h"
#include "pm_settings.h"
static WebServer *s_server = nullptr;

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

static void handle_settings_get() {
  if (!s_server) {
    return;
  }
  PmBirthSpec birth = {};
  const bool have_birth = pm_birth_load(&birth);

  static char page[4096];
  size_t len = 0;
  page[0] = '\0';

  append_html(page, sizeof(page), &len,
              "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
              "content=\"width=device-width,initial-scale=1\"><title>Astrolabe settings</title>"
              "<style>body{margin:0;padding:16px;background:#0e1018;color:#d8dce8;"
              "font-family:system-ui,sans-serif}h1{font-size:1.25rem}label{display:block;"
              "margin:10px 0 4px;font-size:.85rem;color:#9aa3b8}input,button{font-size:1rem;"
              "padding:8px;border-radius:8px;border:1px solid #333;background:#1a1f2e;color:#eee}"
              "button{background:#3d5a80;margin-top:12px}.row{display:flex;gap:8px;flex-wrap:wrap}"
              ".row label{flex:1 1 40%}a{color:#8cf}</style></head><body>");
  append_html(page, sizeof(page), &len, "<h1>Astrolabe settings</h1>");
  if (s_server->hasArg("saved")) {
    append_html(page, sizeof(page), &len, "<p style=\"color:#8d8\">Saved to watch NVS.</p>");
  }
  if (s_server->hasArg("cleared")) {
    append_html(page, sizeof(page), &len, "<p style=\"color:#8d8\">Birth data cleared.</p>");
  }
  append_html(page, sizeof(page), &len, "<p>LAN only. Host: <code>");
  append_html(page, sizeof(page), &len, pm_settings_host_label());
  append_html(page, sizeof(page), &len, "</code></p>");

  char chunk[512];
  snprintf(chunk, sizeof(chunk),
           "<form method=\"POST\" action=\"/settings/birth\"><h2>Birth chart</h2>"
           "<div class=\"row\">"
           "<label>Year<input name=\"year\" type=\"number\" min=\"1900\" max=\"2100\" value=\"%u\"></label>"
           "<label>Month<input name=\"month\" type=\"number\" min=\"1\" max=\"12\" value=\"%u\"></label>"
           "<label>Day<input name=\"day\" type=\"number\" min=\"1\" max=\"31\" value=\"%u\"></label>"
           "</div><div class=\"row\">"
           "<label>Hour<input name=\"hour\" type=\"number\" min=\"0\" max=\"23\" value=\"%u\"></label>"
           "<label>Minute<input name=\"minute\" type=\"number\" min=\"0\" max=\"59\" value=\"%u\"></label>"
           "</div>",
           have_birth ? birth.year : 1972u, have_birth ? birth.month : 5u, have_birth ? birth.day : 6u,
           have_birth ? birth.hour : 11u, have_birth ? birth.minute : 30u);
  append_html(page, sizeof(page), &len, chunk);

  char place_esc[120];
  html_escape_attr(have_birth ? birth.place : "Tallahassee, FL", place_esc, sizeof(place_esc));
  snprintf(chunk, sizeof(chunk),
           "<label>Place<input name=\"place\" maxlength=\"39\" value=\"%s\"></label>"
           "<label>Latitude<input name=\"lat\" type=\"number\" step=\"0.0001\" value=\"%.4f\"></label>"
           "<label>Longitude<input name=\"lon\" type=\"number\" step=\"0.0001\" value=\"%.4f\"></label>"
           "<label>TZ offset (hours from UTC)<input name=\"tz_hours\" type=\"number\" step=\"0.5\" value=\"%.1f\">"
           "</label><button type=\"submit\">Save birth</button></form>",
           place_esc, have_birth ? birth.lat_deg : 30.4383f,
           have_birth ? birth.lon_deg : -84.2807f,
           have_birth ? (birth.tz_offset_sec / 3600.f) : -5.f);
  append_html(page, sizeof(page), &len, chunk);

  append_html(page, sizeof(page), &len,
              "<form method=\"POST\" action=\"/settings/clear\" style=\"margin-top:24px\">"
              "<button type=\"submit\">Clear birth</button></form>"
              "<p style=\"margin-top:32px;font-size:.85rem\">"
              "<a href=\"/screen.bmp\">Screen capture</a> · "
              "<a href=\"/\">Home</a></p></body></html>");

  s_server->send(200, "text/html", page);
}

static void handle_settings_post_birth() {
  if (!s_server) {
    return;
  }
  const int y = s_server->arg("year").toInt();
  const int mo = s_server->arg("month").toInt();
  const int d = s_server->arg("day").toInt();
  const int h = s_server->arg("hour").toInt();
  const int mi = s_server->arg("minute").toInt();
  const float lat = s_server->arg("lat").toFloat();
  const float lon = s_server->arg("lon").toFloat();
  const float tz_h = s_server->arg("tz_hours").toFloat();

  if (y >= 1900 && y <= 2100 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 23 && mi >= 0 &&
      mi <= 59) {
    PmBirthSpec bb = {};
    bb.year = static_cast<uint16_t>(y);
    bb.month = static_cast<uint8_t>(mo);
    bb.day = static_cast<uint8_t>(d);
    bb.hour = static_cast<uint8_t>(h);
    bb.minute = static_cast<uint8_t>(mi);
    bb.lat_deg = lat;
    bb.lon_deg = lon;
    bb.tz_offset_sec = static_cast<int32_t>(tz_h * 3600.f);
    const String place = s_server->arg("place");
    strncpy(bb.place, place.c_str(), sizeof(bb.place) - 1);
    bb.place[sizeof(bb.place) - 1] = '\0';
    bb.valid = true;
    pm_birth_save(&bb);
  }
  s_server->sendHeader("Location", "/settings?saved=1", true);
  s_server->send(303, "text/plain", "");
}

static void handle_settings_clear() {
  if (!s_server) {
    return;
  }
  pm_birth_clear();
  s_server->sendHeader("Location", "/settings?cleared=1", true);
  s_server->send(303, "text/plain", "");
}

void pm_settings_http_register(WebServer *server) {
  if (!server) {
    return;
  }
  s_server = server;
  server->on("/settings", HTTP_GET, handle_settings_get);
  server->on("/settings/birth", HTTP_POST, handle_settings_post_birth);
  server->on("/settings/clear", HTTP_POST, handle_settings_clear);
}
