#!/usr/bin/env python3
"""Generate product-variant .ino files from sketches/Astrolabe/Astrolabe.ino."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "sketches" / "Astrolabe" / "Astrolabe.ino"


def strip_function(text: str, name: str) -> str:
    pat = rf"^static\s+(?:void|bool|const char \*|int)\s+{re.escape(name)}\([^{{]*\{{"
    m = re.search(pat, text, re.MULTILINE)
    if not m:
        return text
    start = m.start()
    i = m.end() - 1
    depth = 0
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(text) and text[end] in "\n\r":
                    end += 1
                return text[:start] + text[end:]
        i += 1
    return text


def replace_enum_astrolabe(text: str) -> str:
    enum = """enum class ClockFace : uint8_t {
  ClassicAnalog = 0,
  Apocalypso,
  DigitalLocal,
  Castalia,
  kNumFaces,
};

static ClockFace g_clock_face = ClockFace::ClassicAnalog;
"""
    return re.sub(
        r"enum class ClockFace : uint8_t \{[^}]+\};\s*\n\s*static ClockFace g_clock_face = [^;]+;",
        enum,
        text,
        count=1,
        flags=re.DOTALL,
    )


def replace_enum_lunasay(text: str) -> str:
    enum = """enum class ClockFace : uint8_t {
  Moon = 0,
  Astrology,
  Fortune,
  DigitalLocal,
  Castalia,
  kNumFaces,
};

static ClockFace g_clock_face = ClockFace::Moon;
"""
    return re.sub(
        r"enum class ClockFace : uint8_t \{[^}]+\};\s*\n\s*static ClockFace g_clock_face = [^;]+;",
        enum,
        text,
        count=1,
        flags=re.DOTALL,
    )


def patch_switch_astrolabe(text: str) -> str:
    body = """  switch (g_clock_face) {
    case ClockFace::ClassicAnalog:
      draw_analog_clock(bg, &tm, pm_time_valid());
      break;
    case ClockFace::Apocalypso:
      draw_apocalypso_face(&tm, pm_time_valid());
      break;
    case ClockFace::DigitalLocal:
      draw_digital_local_face(&tm, pm_time_valid());
      break;
    case ClockFace::Castalia:
      draw_castalia_face();
      break;
    default:
      break;
  }"""
    return re.sub(
        r"  switch \(g_clock_face\) \{.*?  \}\n\n  const int banner_y",
        body + "\n\n  const int banner_y",
        text,
        count=1,
        flags=re.DOTALL,
    )


def patch_switch_lunasay(text: str) -> str:
    body = """  switch (g_clock_face) {
    case ClockFace::Moon:
      draw_moon_face(&tm, pm_time_valid());
      break;
    case ClockFace::Astrology:
      draw_astrology_face(&tm, pm_time_valid(), -1, -1, false);
      break;
    case ClockFace::Fortune:
      draw_fortune_face(&tm, pm_time_valid());
      break;
    case ClockFace::DigitalLocal:
      draw_digital_local_face(&tm, pm_time_valid());
      break;
    case ClockFace::Castalia:
      draw_castalia_face();
      break;
    default:
      break;
  }"""
    return re.sub(
        r"  switch \(g_clock_face\) \{.*?  \}\n\n  const int banner_y",
        body + "\n\n  const int banner_y",
        text,
        count=1,
        flags=re.DOTALL,
    )


def patch_banner_astrolabe(text: str) -> str:
    return re.sub(
        r"  const int banner_y = \(g_clock_face == ClockFace::[\s\S]*?\? 352\s*: 320;",
        """  const int banner_y = (g_clock_face == ClockFace::Apocalypso || g_clock_face == ClockFace::Castalia)
                           ? 352
                           : 320;""",
        text,
        count=1,
    )


def patch_banner_lunasay(text: str) -> str:
    return re.sub(
        r"  const int banner_y = \(g_clock_face == ClockFace::[\s\S]*?\? 352\s*: 320;",
        """  const int banner_y = (g_clock_face == ClockFace::Astrology || g_clock_face == ClockFace::Moon ||
                        g_clock_face == ClockFace::Fortune || g_clock_face == ClockFace::Castalia)
                           ? 352
                           : 320;""",
        text,
        count=1,
    )


ASTRO_FUNCS = [
    "moon_phase_name_from_elong_deg",
    "moon_illum_waxing_from_tp",
    "draw_moon_disk",
    "draw_moon_face",
    "build_moon_voice_message",
    "build_moon_system_prompt",
    "draw_astrology_face",
    "draw_astro_voice_screen",
    "build_astrology_voice_message",
    "build_astrology_system_prompt",
    "zodiac_abbr_from_lon",
    "observer_lat_lon",
    "celestial_polar_xy",
    "draw_celestial_face",
]

ASTROLABE_EXTRA_STRIP = [
    "draw_celestial_face",
    "observer_lat_lon",
    "celestial_polar_xy",
    "moon_phase_name_from_elong_deg",
    "zodiac_abbr_from_lon",
    "astrology_sign_label_radius",
    "astrology_chart_ephemeris_ok",
    "draw_astrology_sign_labels",
]

CASTALIA_VOICE_SYS = '''
static const char kCastaliaVoiceSys[] =
    "You are a calm mindfulness companion on a small round pocket watch with a hue clock. "
    "Help the wearer notice breath, body, and the present moment — warm, brief spoken replies "
    "(under 45 seconds) unless they ask for more. No medical, legal, or financial advice; "
    "no fortune-telling, astrology, or predictions.";
'''

ASTROLABE_STRIP_FUNCS = [
    "draw_spotify_face",
    "spotify_copy_short_line",
    "spotify_hit_transport_bar",
    "draw_calcifer_face",
    "format_calcifer_countdown",
]

LUNA_REMOVE_FUNCS = [
    "draw_apocalypso_face",
    "draw_spotify_face",
    "spotify_copy_short_line",
    "spotify_hit_transport_bar",
    "draw_calcifer_face",
    "draw_analog_clock",
    "draw_hand_radial",
]

FORTUNE_BLOCK = '''
static const char kFortuneVoiceSys[] =
    "You are a thoughtful oracle on a small round watch. The user asks a sincere question. "
    "Offer a brief, metaphor-rich reading (under 60 seconds spoken) — reflective, not deterministic. "
    "No medical, legal, or financial advice; no cruel predictions. "
    "Speak only words to be heard aloud; no stage directions.";

static void draw_fortune_face(const struct tm *tm_local, bool valid_local) {
  const uint16_t c_dim = gfx->color565(140, 130, 170);
  const uint16_t c_hi = gfx->color565(230, 220, 255);
  drawCenteredLine("ORACLE", 72, c_hi, 2, 2);
  drawCenteredLine("hold side button · ask", 108, c_dim, 1, 1);
  if (valid_local && tm_local) {
    char tbuf[40];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_local->tm_hour, tm_local->tm_min);
    drawCenteredLine(tbuf, 134, c_dim, 1, 1);
  }
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2 + 20;
  gfx->drawCircle(cx, cy, 72, gfx->color565(168, 148, 212));
  gfx->drawCircle(cx, cy, 48, gfx->color565(120, 100, 160));
  for (int i = 0; i < 3; ++i) {
    const float ang = static_cast<float>(i) * (2.f * 3.14159265f / 3.f) - 1.5707963f;
    const int px = cx + static_cast<int>(cosf(ang) * 56.f);
    const int py = cy + static_cast<int>(sinf(ang) * 56.f);
    gfx->fillCircle(px, py, 8, gfx->color565(220, 210, 255));
  }
  drawCenteredLine("tap other button · replay", 318, c_dim, 1, 1);
}

static bool build_fortune_voice_message(char *buf, size_t cap) {
  if (!buf || cap < 64 || !pm_wifi_connected()) {
    return false;
  }
  snprintf(buf, cap,
           "The user is on the LunaSay oracle face and wants a spoken reflection. "
           "Invite them to ask their question in the next line.");
  return true;
}
'''


def remove_astro_includes_astrolabe(text: str) -> str:
    for inc in [
        '#include "pm_transit.h"\n',
        '#include "pm_astro_highlight.h"\n',
        '#include "pm_birth_nvs.h"\n',
        '#include "pm_stars.h"\n',
        '#include "pm_moon_draw.h"\n',
        '#include "pm_spotify.h"\n',
        '#include "pm_calcifer.h"\n',
    ]:
        text = text.replace(inc, "")
    return text


def scrub_astro_vars_astrolabe(text: str) -> str:
    lines = []
    for line in text.splitlines(keepends=True):
        if "g_astro_voice" in line or "g_moon_voice" in line or "s_astro_" in line:
            if "static " in line:
                continue
        lines.append(line)
    out = "".join(lines)
    out = out.replace("g_astro_voice_active = false;\n", "")
    out = out.replace("g_astro_voice_pcm = false;\n", "")
    out = out.replace("g_moon_voice_pcm = false;\n", "")
    out = out.replace("s_astro_play_armed = false;\n", "")
    out = out.replace("        s_astro_play_armed = false;\n", "")
    out = out.replace("          if (g_astro_voice_active) {\n            draw_astro_voice_screen(\"voice start fail\", -1, -1, false);\n          }\n", "")
    return out


def remove_luna_excludes(text: str) -> str:
    for inc in ['#include "pm_spotify.h"\n', '#include "pm_calcifer.h"\n', '#include "pm_stars.h"\n']:
        text = text.replace(inc, "")
    return text


def remove_astro_state(text: str) -> str:
    lines = []
    skip_prefixes = (
        "static constexpr uint8_t k_tv_astro",
        "static constexpr uint8_t k_tv_moon",
        "static char g_moon_",
        "static char g_astrology_",
        "static bool g_moon_",
        "static bool g_astro_",
        "static bool s_astro_",
        "static PmAstroHighlightPlan",
    )
    for line in text.splitlines(keepends=True):
        if any(line.startswith(p) for p in skip_prefixes):
            continue
        if "g_astro_voice_active" in line and "static" in line:
            continue
        if "g_astro_voice_pcm" in line and "static" in line:
            continue
        lines.append(line)
    return "".join(lines)


def remove_spotify_state_lunasay(text: str) -> str:
    blocks = [
        "static PmSpotifyStatus g_spotify_ui",
        "static bool s_spotify_have_data",
        "static uint32_t s_last_spotify_poll_ms",
        "static PmCalciferStatus g_calcifer_ui",
        "static bool s_calcifer_have_data",
        "static uint32_t s_last_calcifer_poll_ms",
        "static constexpr int kSpotify",
        "static bool g_calcifer_briefing",
    ]
    lines = []
    for line in text.splitlines(keepends=True):
        if any(b in line for b in blocks):
            continue
        lines.append(line)
    return "".join(lines)


def scrub_calcifer_refs(text: str) -> str:
    text = text.replace("g_calcifer_briefing = false;\n", "")
    text = text.replace("      g_calcifer_briefing = false;\n", "")
    text = text.replace("  g_calcifer_briefing = false;\n", "")
    return text


def scrub_astro_face_refs_astrolabe(text: str) -> str:
    text = re.sub(
        r"\n  if \(g_clock_face == ClockFace::Astrology\) \{\n    draw_astrology_sign_labels\(-1\);\n  \}\n",
        "\n",
        text,
        count=1,
    )
    text = re.sub(
        r"        if \(\(g_clock_face == ClockFace::Astrology \|\| g_clock_face == ClockFace::Celestial\) && valid\) \{\n          s_prev_astro_epoch_min = epoch_min_bucket;\n        \}\n",
        "",
        text,
        count=1,
    )
    text = text.replace("|| g_clock_face == ClockFace::Celestial", "")
    return text


def patch_thinking_lunasay(text: str) -> str:
    """Remove CalDAV agenda branch; LunaSay has no schedule faces."""
    text = text.replace(
        """        bool started = false;
        if (g_calcifer_briefing) {
          started = pm_voice_begin_clock_agenda(&g_voice_result);
        } else if (g_text_voice_route == k_tv_moon) {""",
        """        bool started = false;
        if (g_text_voice_route == k_tv_moon) {""",
    )
    text = re.sub(
        r"        bool started = false;\n          started = pm_voice_begin_clock_agenda\(&g_voice_result\);\n        \} else if",
        "        bool started = false;\n        if",
        text,
        count=1,
    )
    text = re.sub(
        r"        bool started = false;\n        if \(g_calcifer_briefing\) \{\n          started = pm_voice_begin_clock_agenda\(&g_voice_result\);\n        \} else \{\n          started = pm_voice_begin_pcm\(g_pcm, g_pcm_len, kCastaliaVoiceSys, &g_voice_result\);\n        \}",
        "        const bool started = pm_voice_begin_pcm(g_pcm, g_pcm_len, kCastaliaVoiceSys, &g_voice_result);",
        text,
        count=1,
    )
    return text


def strip_moon_boot_handlers(text: str) -> str:
    """Remove dedicated Moon/Astrology BOOT blocks; keep generic replay."""
    text = re.sub(
        r"\n  if \(g_state == AppState::kClock && g_clock_face == ClockFace::Moon &&[\s\S]*?g_state = AppState::kThinking;\n    \}\n  \}",
        "",
        text,
        count=1,
    )
    text = re.sub(
        r"\n  if \(g_state == AppState::kClock && g_clock_face == ClockFace::Astrology &&[\s\S]*?g_state = AppState::kThinking;\n    \}\n  \}",
        "",
        text,
        count=1,
    )
    text = text.replace(
        "      g_clock_face != ClockFace::Astrology && g_clock_face != ClockFace::Moon) {",
        "      false) {",
    )
    return text


def add_fortune_handlers_lunasay(text: str) -> str:
    fortune_boot = '''
  if (g_state == AppState::kClock && g_clock_face == ClockFace::Fortune &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (!pm_wifi_connected()) {
      g_clock_repaint_pending = true;
    } else if (!build_fortune_voice_message(g_astrology_voice_msg, sizeof(g_astrology_voice_msg))) {
      g_clock_repaint_pending = true;
    } else {
      pm_voice_result_free(&g_voice_result);
      g_voice_use_message = true;
      g_text_voice_route = k_tv_astro;
      g_astro_voice_active = true;
      g_astro_voice_pcm = false;
      s_astro_voice_armed = false;
      s_astro_play_armed = false;
      g_state = AppState::kThinking;
    }
  }

'''
    marker = "  static uint32_t s_ptt_press_ms = 0;"
    if marker in text and "ClockFace::Fortune &&\n      (side_ev" not in text:
        text = text.replace(marker, fortune_boot + marker)
    text = text.replace(
        "g_clock_face != ClockFace::Astrology && g_clock_face != ClockFace::Moon)",
        "g_clock_face != ClockFace::Astrology && g_clock_face != ClockFace::Moon &&\n      g_clock_face != ClockFace::Fortune)",
    )
    return text


def strip_spotify_gesture_block(text: str) -> str:
    return re.sub(
        r"    \} else if \(g_state == AppState::kClock && g_clock_face == ClockFace::Spotify &&[\s\S]*?g_clock_repaint_pending = true;\n    \} else if",
        "    } else if",
        text,
        count=1,
    )


def strip_calcifer_boot_else(text: str) -> str:
    """Drop CalDAV agenda-on-BOOT branch (ClassicAnalog / Calcifer faces absent on splits)."""
    return re.sub(
        r"    \} else \{\n      const bool want_calcifer =[\s\S]*?g_state = AppState::kThinking;\n      \}\n    \}\n  \}",
        "    }\n  }",
        text,
        count=1,
    )


def strip_spotify_calcifer_product(text: str) -> str:
    t = remove_spotify_state_lunasay(text)
    for fn in ASTROLABE_STRIP_FUNCS:
        t = strip_function(t, fn)
    t = strip_spotify_gesture_block(t)
    t = re.sub(r"\n      if \(g_clock_face != ClockFace::Spotify\) \{[\s\S]*?\}\n", "\n", t)
    t = re.sub(r"\n      if \(g_clock_face != ClockFace::CalciferCountdown\) \{[\s\S]*?\}\n", "\n", t)
    t = re.sub(r"\n      const bool spotify_stale =[\s\S]*?;\n", "\n", t)
    t = re.sub(r"\n      const bool calcifer_stale =[\s\S]*?;\n", "\n", t)
    t = t.replace("|| spotify_stale || calcifer_stale", "")
    t = t.replace(
        """        if (g_clock_face == ClockFace::Spotify && pm_wifi_connected()) {
          if (!s_spotify_have_data || spotify_stale) {
            pm_spotify_refresh(&g_spotify_ui);
            s_last_spotify_poll_ms = now;
            s_spotify_have_data = true;
          }
        }
        if (g_clock_face == ClockFace::CalciferCountdown && pm_wifi_connected() && valid) {
          if (!s_calcifer_have_data || calcifer_stale) {
            (void)pm_calcifer_fetch(&g_calcifer_ui, epoch);
            s_last_calcifer_poll_ms = now;
            s_calcifer_have_data = true;
          }
        }
""",
        "",
    )
    t = t.replace(
        "sec_tick && g_clock_face != ClockFace::Castalia && g_clock_face != ClockFace::CalciferCountdown",
        "sec_tick && g_clock_face != ClockFace::Castalia",
    )
    t = re.sub(r"\n      const bool calcifer_sec =[\s\S]*?;\n", "\n", t)
    t = t.replace("|| calcifer_sec", "")
    t = re.sub(
        r"    \} else if \(g_state == AppState::kClock && g_clock_face == ClockFace::Spotify &&[\s\S]*?g_clock_repaint_pending = true;\n",
        "",
        t,
        count=1,
    )
    return t


def simplify_ptt_astrolabe(text: str) -> str:
    """PTT: no astro/moon branches — general faculty voice."""
    old = """      if (ptt_armed && g_pcm) {
        if (g_clock_face == ClockFace::Astrology) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_time_valid()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need time");
            g_clock_repaint_pending = true;
            break;
          }
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_moon_voice_pcm = false;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
          memset(&g_astro_highlight_plan, 0, sizeof(g_astro_highlight_plan));
        } else if (g_clock_face == ClockFace::Moon) {
          if (!pm_wifi_connected() || !pm_time_valid()) {
            g_clock_repaint_pending = true;
            break;
          }
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_moon_voice_pcm = true;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
        } else {
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_moon_voice_pcm = false;
        }"""
    new = """      if (ptt_armed && g_pcm) {
        g_astro_voice_active = false;
        g_astro_voice_pcm = false;
        g_moon_voice_pcm = false;"""
    return text.replace(old, new)


def simplify_ptt_lunasay(text: str) -> str:
    old = """        } else {
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_moon_voice_pcm = false;
        }
        reset_recording_buffer();"""
    new = """        } else if (g_clock_face == ClockFace::Fortune) {
          if (!pm_wifi_connected()) {
            g_clock_repaint_pending = true;
            break;
          }
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_moon_voice_pcm = false;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
        } else {
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_moon_voice_pcm = false;
        }
        reset_recording_buffer();"""
    return text.replace(old, new)


def patch_header(text: str, product: str, title: str) -> str:
    text = re.sub(r"^// Astrolabe.*\n", f"// {title}\n", text, count=1)
    text = text.replace('Serial.println("Astrolabe ready");', f'Serial.println("{product} ready");')
    return text


def simplify_voice_fsm_astrolabe(text: str) -> str:
    text = re.sub(
        r"static const char kAstroVoiceSys\[\] =[\s\S]*?spoken aloud\.\";\n",
        CASTALIA_VOICE_SYS + "\n",
        text,
        count=1,
    )
    text = re.sub(
        r"      if \(g_astro_voice_active\) \{[\s\S]*?gfx->flush\(\);\n      \} else \{\n        draw_voice_wave_screen\(false, now, \"listening\"\);\n      \}",
        '      draw_voice_wave_screen(false, now, "listening");',
        text,
        count=1,
    )
    text = re.sub(
        r"        bool started = false;\n        if \(g_calcifer_briefing\) \{[\s\S]*?started = pm_voice_begin_pcm\(g_pcm, g_pcm_len, sys, &g_voice_result\);\n        \}",
        """        bool started = false;
        if (g_calcifer_briefing) {
          started = pm_voice_begin_clock_agenda(&g_voice_result);
        } else {
          started = pm_voice_begin_pcm(g_pcm, g_pcm_len, kCastaliaVoiceSys, &g_voice_result);
        }""",
        text,
        count=1,
    )
    text = re.sub(
        r"      if \(g_astro_voice_active\) \{\n        draw_astro_voice_screen\(nullptr, -1, -1, false, thinking_progress_now\(\)\);\n      \} else if \(g_text_voice_route == k_tv_moon\) \{[\s\S]*?gfx->flush\(\);\n      \} else \{\n        draw_clock_face\(thinking_progress_now\(\)\);\n      \}",
        "      draw_clock_face(thinking_progress_now());",
        text,
        count=1,
    )
    text = re.sub(
        r"      if \(vs != PmVoiceStatus::DoneOk\) \{\n        if \(g_astro_voice_active\) \{[\s\S]*?break;\n      \}",
        """      if (vs != PmVoiceStatus::DoneOk) {
        gfx->fillScreen(RGB565_BLACK);
        drawCenteredLine("voice error", 200, RGB565_RED, 2, 2);
        drawCenteredLine(pm_voice_last_error(), 232, gfx->color565(180, 120, 120), 1, 1);
        gfx->flush();
        delay(1500);
        pm_voice_result_free(&g_voice_result);
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }""",
        text,
        count=1,
    )
    text = re.sub(
        r"      if \(!g_voice_result\.mp3 \|\| g_voice_result\.mp3_len < 64\) \{\n        if \(g_astro_voice_active\) \{[\s\S]*?break;\n        \}\n        if \(g_voice_result",
        "      if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {\n        if (g_voice_result",
        text,
        count=1,
    )
    text = re.sub(
        r"      if \(g_astro_voice_active\) \{\n        pm_astro_highlight_build[\s\S]*?\n      \}\n      if \(g_voice_result\.mp3",
        "      if (g_voice_result.mp3",
        text,
        count=1,
    )
    text = re.sub(
        r"      if \(g_astro_voice_active\) \{\n        if \(!s_astro_play_armed\) \{[\s\S]*?break;\n      \}\n      if \(!g_voice_result\.mp3",
        "      if (!g_voice_result.mp3",
        text,
        count=1,
    )
    text = text.replace("poll_serial_birth_commands();\n", "")
    text = re.sub(r"\nstatic void poll_serial_birth_commands\(\) \{[\s\S]*?\n\}\n", "\n", text, count=1)
    return text


def finalize_lunasay(text: str) -> str:
    text = patch_banner_lunasay(text)
    text = re.sub(
        r"\(g_clock_face == ClockFace::Astrology \|\| g_clock_face == ClockFace::Celestial\)",
        "g_clock_face == ClockFace::Astrology",
        text,
    )
    text = text.replace("|| g_clock_face == ClockFace::Celestial", "")
    text = re.sub(
        r"        if \(\(g_clock_face == ClockFace::Astrology \|\| g_clock_face == ClockFace::Celestial\) && valid\)",
        "        if (g_clock_face == ClockFace::Astrology && valid)",
        text,
    )
    return text


def gen_astrolabe(src: str) -> str:
    t = src
    t = remove_astro_includes_astrolabe(t)
    t = remove_astro_state(t)
    for fn in ASTRO_FUNCS:
        t = strip_function(t, fn)
    for fn in ASTROLABE_EXTRA_STRIP:
        t = strip_function(t, fn)
    t = strip_moon_boot_handlers(t)
    t = strip_spotify_calcifer_product(t)
    t = strip_calcifer_boot_else(t)
    t = simplify_ptt_astrolabe(t)
    t = replace_enum_astrolabe(t)
    t = patch_switch_astrolabe(t)
    t = patch_banner_astrolabe(t)
    t = simplify_voice_fsm_astrolabe(t)
    t = patch_thinking_lunasay(t)
    t = scrub_calcifer_refs(t)
    t = scrub_astro_face_refs_astrolabe(t)
    t = scrub_astro_vars_astrolabe(t)
    t = patch_header(t, "Astrolabe", "Astrolabe — mindfulness hue clock & calm voice")
    t = re.sub(r"\n      const bool astro_repaint =[\s\S]*?;\n", "\n", t)
    t = t.replace("|| astro_repaint", "")
    return t


def gen_lunasay(src: str) -> str:
    t = src
    t = remove_luna_excludes(t)
    t = strip_spotify_calcifer_product(t)
    for fn in LUNA_REMOVE_FUNCS:
        t = strip_function(t, fn)
    for fn in ASTRO_FUNCS:
        if fn in ("draw_apocalypso_face", "draw_label_at_polar"):
            continue
        if fn == "draw_label_at_polar":
            continue
    # keep draw_label_at_polar for astrology labels - was removed with apocalypso strip in LUNA_REMOVE
    # draw_astrology uses draw_label_at_polar - need to keep it on lunasay
    t = strip_function(t, "draw_apocalypso_face")
    t = strip_function(t, "draw_spotify_face")
    t = strip_function(t, "spotify_copy_short_line")
    t = strip_function(t, "spotify_hit_transport_bar")
    t = strip_function(t, "draw_calcifer_face")
    t = strip_function(t, "draw_analog_clock")
    t = strip_function(t, "draw_hand_radial")
    t = strip_function(t, "draw_celestial_face")
    t = strip_calcifer_boot_else(t)
    # Insert fortune before draw_clock_face
    t = t.replace("static void draw_clock_face(float thinking_progress", FORTUNE_BLOCK + "\nstatic void draw_clock_face(float thinking_progress")
    t = replace_enum_lunasay(t)
    t = patch_switch_lunasay(t)
    t = add_fortune_handlers_lunasay(t)
    t = finalize_lunasay(t)
    t = simplify_ptt_lunasay(t)
    t = patch_thinking_lunasay(t)
    t = scrub_calcifer_refs(t)
    t = patch_header(t, "LunaSay", "LunaSay — moon home, chart & oracle")
    # Fortune thinking uses astro voice screen or generic
    t = t.replace(
        "started = pm_voice_begin_message(g_astrology_voice_msg, kAstroVoiceSys, &g_voice_result);",
        "started = pm_voice_begin_message(g_astrology_voice_msg,\n              g_clock_face == ClockFace::Fortune ? kFortuneVoiceSys : kAstroVoiceSys,\n              &g_voice_result);",
    )
    return t


def main() -> int:
    src = SRC.read_text()
    astro = gen_astrolabe(src)
    luna = gen_lunasay(src)
    out_a = ROOT / "sketches" / "Astrolabe" / "Astrolabe.ino"
    out_l = ROOT.parent / "lunasay" / "sketches" / "LunaSay" / "LunaSay.ino"
    out_a.write_text(astro)
    out_l.write_text(luna)
    print(f"Wrote {out_a} ({len(astro.splitlines())} lines)")
    print(f"Wrote {out_l} ({len(luna.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
