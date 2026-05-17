#!/usr/bin/env python3
"""Extract clock-face draw code from Astrolabe.ino into faces/<name>/ modules."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INO = ROOT / "sketches" / "Astrolabe" / "Astrolabe.ino"
FACES = ROOT / "sketches" / "Astrolabe" / "faces"

MODULES: dict[str, list[str]] = {
    "shared": [
        "color565FromHsv",
        "drawCenteredLine",
        "draw_hand_radial",
        "draw_label_at_polar",
        "draw_radial_annulus_slice",
        "draw_circumference_rainbow_24h",
        "draw_thinking_progress_ring",
        "draw_voice_waves_overlay",
        "draw_voice_wave_screen",
    ],
    "classic_analog": ["draw_analog_clock"],
    "apocalypso": ["draw_apocalypso_face"],
    "digital": ["draw_digital_local_face"],
    "spotify": ["spotify_copy_short_line", "spotify_hit_transport_bar", "draw_spotify_face"],
    "calcifer": ["format_calcifer_countdown", "draw_calcifer_face"],
    "moon": [
        "moon_phase_name_from_elong_deg",
        "draw_moon_face",
        "build_moon_voice_message",
        "build_moon_system_prompt",
    ],
    "astrology": [
        "zodiac_abbr_from_lon",
        "astrology_sign_label_radius",
        "astrology_planet_radius",
        "draw_astrology_face",
        "draw_astro_voice_screen",
        "build_astrology_voice_message",
        "build_astrology_system_prompt",
    ],
    "castalia": ["draw_castalia_face"],
}

REGISTRY = ["cycle_clock_face", "draw_clock_face"]

RENAME = {
    "color565FromHsv": "pm_face_color565_from_hsv",
    "drawCenteredLine": "pm_face_draw_centered_line",
    "draw_hand_radial": "pm_face_draw_hand_radial",
    "draw_label_at_polar": "pm_face_draw_label_at_polar",
    "draw_radial_annulus_slice": "pm_face_draw_radial_annulus_slice",
    "draw_circumference_rainbow_24h": "pm_face_draw_circumference_rainbow_24h",
    "draw_thinking_progress_ring": "pm_face_draw_thinking_progress_ring",
    "draw_voice_waves_overlay": "pm_face_draw_voice_waves_overlay",
    "draw_voice_wave_screen": "pm_face_draw_voice_wave_screen",
    "draw_analog_clock": "pm_face_classic_analog_draw",
    "draw_apocalypso_face": "pm_face_apocalypso_draw",
    "draw_digital_local_face": "pm_face_digital_draw",
    "spotify_copy_short_line": "pm_face_spotify_copy_short_line",
    "spotify_hit_transport_bar": "pm_face_spotify_hit_transport_bar",
    "draw_spotify_face": "pm_face_spotify_draw",
    "format_calcifer_countdown": "pm_face_calcifer_format_countdown",
    "draw_calcifer_face": "pm_face_calcifer_draw",
    "moon_phase_name_from_elong_deg": "pm_face_moon_phase_name",
    "draw_moon_face": "pm_face_moon_draw",
    "build_moon_voice_message": "pm_face_moon_build_voice_message",
    "build_moon_system_prompt": "pm_face_moon_build_system_prompt_impl",
    "zodiac_abbr_from_lon": "pm_face_zodiac_abbr",
    "astrology_sign_label_radius": "pm_face_astrology_sign_label_radius",
    "astrology_planet_radius": "pm_face_astrology_planet_radius",
    "draw_astrology_face": "pm_face_astrology_draw",
    "draw_astro_voice_screen": "pm_face_astrology_draw_voice_screen",
    "build_astrology_voice_message": "pm_face_astrology_build_voice_message",
    "build_astrology_system_prompt": "pm_face_astrology_build_system_prompt_impl",
    "observer_lat_lon": "pm_face_celestial_observer_lat_lon",
    "celestial_polar_xy": "pm_face_celestial_polar_xy",
    "draw_celestial_face": "pm_face_celestial_draw",
    "draw_castalia_face": "pm_face_castalia_draw",
    "draw_clock_face": "pm_faces_draw",
    "cycle_clock_face": "pm_faces_cycle",
    "is_commonplace_home_face": "pm_faces_is_commonplace_home",
}

HFILES = {
    "classic_analog": "pm_face_classic_analog.h",
    "digital": "pm_face_digital.h",
    "apocalypso": "pm_face_apocalypso.h",
    "spotify": "pm_face_spotify.h",
    "calcifer": "pm_face_calcifer.h",
    "moon": "pm_face_moon.h",
    "astrology": "pm_face_astrology.h",
    "celestial": "pm_face_celestial.h",
    "castalia": "pm_face_castalia.h",
}

INCLUDES = {
    "shared": [
        "#include <Arduino_GFX_Library.h>",
        "#include <cmath>",
        '#include "pin_config.h"',
        '#include "pm_display.h"',
        '#include "pm_wifi_ntp.h"',
    ],
    "classic_analog": ['#include <cmath>', '#include "pin_config.h"', '#include "pm_display.h"'],
    "apocalypso": ['#include <cmath>', '#include <cstdio>', '#include "pin_config.h"', '#include "pm_display.h"'],
    "digital": ['#include <cstdio>', '#include "pin_config.h"', '#include "pm_display.h"'],
    "spotify": [
        '#include "pm_spotify.h"',
        '#include "pm_wifi_ntp.h"',
        "#include <cstdio>",
        "#include <cstring>",
        '#include "pin_config.h"',
        '#include "pm_display.h"',
    ],
    "calcifer": [
        '#include "pm_calcifer.h"',
        '#include "pm_wifi_ntp.h"',
        "#include <cstdio>",
        "#include <ctime>",
        '#include "pin_config.h"',
        '#include "pm_display.h"',
    ],
    "moon": [
        '#include "pm_moon_draw.h"',
        '#include "pm_transit.h"',
        '#include "pm_wifi_ntp.h"',
        "#include <cmath>",
        "#include <cstdio>",
        "#include <ctime>",
        '#include "pin_config.h"',
        '#include "pm_display.h"',
    ],
    "astrology": [
        '#include "pm_birth_nvs.h"',
        '#include "pm_transit.h"',
        '#include "pm_wifi_ntp.h"',
        '#include "pm_zodiac_glyphs.h"',
        "#include <cmath>",
        "#include <cstdio>",
        "#include <cstring>",
        "#include <ctime>",
        '#include "pin_config.h"',
        '#include "pm_display.h"',
    ],
    "celestial": [
        '#include "pm_birth_nvs.h"',
        '#include "pm_stars.h"',
        '#include "pm_transit.h"',
        '#include "pm_wifi_ntp.h"',
        "#include <cmath>",
        "#include <cstdio>",
        "#include <ctime>",
        '#include "pin_config.h"',
        '#include "pm_display.h"',
    ],
    "castalia": ['#include "pm_castalia_auth.h"', '#include "pm_wifi_ntp.h"', '#include "pin_config.h"', '#include "pm_display.h"'],
}


def extract_fn(text: str, name: str) -> str | None:
    # Require a simple return type so we don't span from an early `static` global to a
    # later function name mentioned in comments (e.g. "draw_clock_face" in a docstring).
    pat = (
        rf"^static\s+(?:(?:const\s+char\s*\*\s*)|(?:void|bool|int|size_t|uint16_t|float|double)\s+)"
        rf"{re.escape(name)}\([^{{]*\{{"
    )
    m = re.search(pat, text, re.MULTILINE)
    if not m:
        return None
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
                return text[start:end]
        i += 1
    return None


def strip_static(defn: str) -> str:
    return re.sub(r"^static\s+", "", defn, count=1, flags=re.MULTILINE)


def to_decl(defn: str) -> str:
    s = strip_static(defn)
    s = re.sub(r"^static\s+constexpr\s+", "constexpr ", s)
    s = re.sub(r"^static\s+const\s+char\s+", "const char ", s)
    return re.sub(r"\s*\{[\s\S]*$", ";", s).strip()


def xform(code: str) -> str:
    code = code.replace("gfx->", "pm_gfx->").replace("color565FromHsv(gfx", "pm_face_color565_from_hsv(pm_gfx")
    for old, new in sorted(RENAME.items(), key=lambda x: -len(x[0])):
        code = re.sub(rf"\b{re.escape(old)}\b", new, code)
    code = code.replace("g_clock_face", "s_clock_face")
    code = code.replace("g_clock_bg565", "s_clock_bg565")
    code = code.replace("g_analog_saved_local_h", "s_analog_saved_local_h")
    code = code.replace("g_analog_saved_local_m", "s_analog_saved_local_m")
    code = code.replace("g_moon_sys_prompt", "s_moon_sys_prompt")
    code = code.replace("g_astrology_voice_msg", "s_astrology_voice_msg")
    code = code.replace("g_astrology_sys_prompt", "s_astrology_sys_prompt")
    code = code.replace("kTwoPi", "pm_face_k_two_pi").replace("kPi", "pm_face_k_pi")
    code = code.replace("k_clock_face_hsv_s", "pm_face_hsv_s").replace("k_clock_face_hsv_v", "pm_face_hsv_v")
    return code


def main() -> int:
    source = INO.read_text()
    if "void setup()" not in source or "void loop()" not in source:
        print("refusing: ino missing setup/loop", file=sys.stderr)
        return 1

    all_names: list[str] = []
    for names in MODULES.values():
        all_names.extend(names)
    all_names.extend(REGISTRY)

    chunks: dict[str, str] = {}
    for name in all_names:
        raw = extract_fn(source, name)
        if not raw:
            print(f"missing function: {name}", file=sys.stderr)
            return 1
        chunks[name] = xform(strip_static(raw))

    if extract_fn(source, "is_commonplace_home_face"):
        REGISTRY.insert(0, "is_commonplace_home_face")
        chunks["is_commonplace_home_face"] = xform(strip_static(extract_fn(source, "is_commonplace_home_face") or ""))
        all_names.append("is_commonplace_home_face")

    if extract_fn(source, "draw_celestial_face"):
        MODULES["celestial"] = ["observer_lat_lon", "celestial_polar_xy", "draw_celestial_face"]
        for n in MODULES["celestial"]:
            raw = extract_fn(source, n)
            if raw:
                chunks[n] = xform(strip_static(raw))
                all_names.append(n)

    # display shim
    (ROOT / "sketches" / "Astrolabe" / "pm_display.h").write_text(
        "#pragma once\n\nclass Arduino_Canvas;\n\nextern Arduino_Canvas *pm_gfx;\nvoid pm_display_bind(Arduino_Canvas *canvas);\n"
    )
    (ROOT / "sketches" / "Astrolabe" / "pm_display.cpp").write_text(
        '#include "pm_display.h"\n\nArduino_Canvas *pm_gfx = nullptr;\nvoid pm_display_bind(Arduino_Canvas *canvas) { pm_gfx = canvas; }\n'
    )

    FACES.mkdir(parents=True, exist_ok=True)

    shared_decls = [xform(to_decl(extract_fn(source, n) or "")) for n in MODULES["shared"]]
    (FACES / "shared").mkdir(parents=True, exist_ok=True)
    (FACES / "shared" / "pm_face_draw.h").write_text(
        "#pragma once\n\n#include <Arduino_GFX_Library.h>\n#include <cstdint>\n\n"
        "constexpr float pm_face_k_pi = 3.14159265f;\n"
        "constexpr float pm_face_k_two_pi = pm_face_k_pi * 2.f;\n"
        "constexpr float pm_face_hsv_s = 0.75f;\n"
        "constexpr float pm_face_hsv_v = 0.14f;\n\n"
        + "\n".join(shared_decls)
        + "\n"
    )
    (FACES / "shared" / "pm_face_draw.cpp").write_text(
        '#include "faces/shared/pm_face_draw.h"\n'
        + "\n".join(f"#include {h}" if h.startswith("<") else f'#include "{h}"' for h in INCLUDES["shared"][1:])
        + "\n\n"
        + "\n\n".join(chunks[n] for n in MODULES["shared"])
        + "\n"
    )

    for mod, names in MODULES.items():
        d = FACES / mod
        d.mkdir(exist_ok=True)
        hfile = HFILES.get(mod, f"pm_face_{mod}.h")
        decls = [xform(to_decl(extract_fn(source, n) or "")) for n in names]
        extra = ""
        if mod == "spotify":
            extra = "\nconstexpr int pm_face_spotify_bar_y = 238;\nconstexpr int pm_face_spotify_bar_h = 62;\nconstexpr int pm_face_spotify_bar_pad = 20;\n"
        if mod == "moon":
            extra += "\nbool pm_face_moon_build_system_prompt(char *out, size_t cap);\n"
        if mod == "astrology":
            extra += (
                "\nbool pm_face_astrology_build_system_prompt(char *voice_msg, size_t voice_cap, "
                "char *sys_out, size_t sys_cap);\n"
            )
        (d / hfile).write_text(
            "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nstruct tm;\nstruct PmTransitPositions;\n\n"
            + "\n".join(decls)
            + extra
            + "\n"
        )
        cpp = f'#include "faces/{mod}/{hfile}"\n#include "faces/shared/pm_face_draw.h"\n'
        cpp += "\n".join(f'#include "{h}"' for h in INCLUDES[mod]) + "\n\n"
        if mod == "spotify":
            cpp += "extern PmSpotifyStatus g_spotify_ui;\n\n"
        if mod == "calcifer":
            cpp += "extern PmCalciferStatus g_calcifer_ui;\n\n"
        if mod == "moon":
            cpp += "static char s_moon_sys_prompt[640];\n\n"
        if mod == "astrology":
            cpp += "static char s_astrology_voice_msg[2200];\nstatic char s_astrology_sys_prompt[2800];\n\n"
        body = "\n\n".join(chunks[n] for n in names)
        if mod == "spotify":
            body = body.replace("kSpotifyBarY", "pm_face_spotify_bar_y").replace(
                "kSpotifyBarH", "pm_face_spotify_bar_h"
            ).replace("kSpotifyBarPad", "pm_face_spotify_bar_pad")
        if mod == "classic_analog":
            cpp += (
                "constexpr int pm_face_analog_cx = LCD_WIDTH / 2;\n"
                "constexpr int pm_face_analog_cy = LCD_HEIGHT / 2;\n"
                "constexpr int pm_face_analog_r = 138;\n"
                "constexpr int pm_face_analog_sec_len = pm_face_analog_r - 10;\n\n"
            )
            body = (
                body.replace("kAnalogCx", "pm_face_analog_cx")
                .replace("kAnalogCy", "pm_face_analog_cy")
                .replace("kAnalogR", "pm_face_analog_r")
                .replace("kAnalogSecLen", "pm_face_analog_sec_len")
            )
        (d / hfile.replace(".h", ".cpp")).write_text(cpp + body + "\n")

    # moon / astrology wrappers
    moon_cpp = (FACES / "moon" / "pm_face_moon.cpp").read_text()
    if "pm_face_moon_build_system_prompt(char *out" not in moon_cpp:
        (FACES / "moon" / "pm_face_moon.cpp").write_text(
            moon_cpp
            + """
bool pm_face_moon_build_system_prompt(char *out, size_t cap) {
  if (!out || cap < 8 || !pm_face_moon_build_system_prompt_impl()) {
    return false;
  }
  strncpy(out, s_moon_sys_prompt, cap - 1);
  out[cap - 1] = '\\0';
  return out[0] != '\\0';
}
"""
        )

    astro_path = FACES / "astrology" / "pm_face_astrology.cpp"
    astro = astro_path.read_text()
    if "pm_face_astrology_build_system_prompt(char *voice_msg" not in astro:
        astro_path.write_text(
            astro
            + """
bool pm_face_astrology_build_system_prompt(char *voice_msg, size_t voice_cap, char *sys_out, size_t sys_cap) {
  if (!pm_face_astrology_build_voice_message(voice_msg, voice_cap)) {
    return false;
  }
  static const char kAstroVoiceSys[] =
      "You are a warm, articulate astrologer speaking aloud for a tiny round watch. Use tropical zodiac. "
      "Chart snapshot data is provided below. If the user asks a question, answer it using those positions; "
      "if they did not ask a question, give ONE flowing mini-reading (under 90 seconds spoken) about today's "
      "transits versus their natal Sun and anything else notable. "
      "No medical or legal advice; reflective insight only, not deterministic fate. "
      "Do not claim arc-minute precision from the numbers. "
      "Do not use asterisk stage directions or emotes (e.g. *smiles*); output only words to be spoken aloud.";
  const int n = snprintf(sys_out, sys_cap, "%s\\n\\nChart snapshot:\\n%s", kAstroVoiceSys, voice_msg);
  return n > 0 && static_cast<size_t>(n) < sys_cap;
}
"""
        )

    enum_m = re.search(r"enum class ClockFace : uint8_t \{[^}]+\};", source, re.DOTALL)
    enum_block = enum_m.group(0) if enum_m else ""

    (FACES / "pm_faces.h").write_text(
        f"""#pragma once

#include <cstdint>

{enum_block}

ClockFace pm_faces_current(void);
void pm_faces_set(ClockFace face);
void pm_faces_cycle(int delta);
bool pm_faces_is_commonplace_home(void);
bool pm_faces_banner_low(void);
void pm_faces_draw(float thinking_progress = -1.f);
uint16_t pm_faces_last_bg565(void);
bool pm_faces_local_hm_changed(int hour, int min);
"""
    )

    clock = """#include "faces/pm_faces.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <ctime>

#include "faces/apocalypso/pm_face_apocalypso.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/castalia/pm_face_castalia.h"
#include "faces/classic_analog/pm_face_classic_analog.h"
#include "faces/digital/pm_face_digital.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/spotify/pm_face_spotify.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

static ClockFace s_clock_face = ClockFace::ClassicAnalog;
static uint16_t s_clock_bg565 = 0;
static int s_analog_saved_local_h = -1;
static int s_analog_saved_local_m = -1;

ClockFace pm_faces_current(void) { return s_clock_face; }
void pm_faces_set(ClockFace face) { s_clock_face = face; }

"""
    clock += "\n\n".join(chunks[n] for n in REGISTRY)
    clock += """

bool pm_faces_banner_low(void) {
  const ClockFace f = s_clock_face;
  return f == ClockFace::Apocalypso || f == ClockFace::Spotify || f == ClockFace::Astrology ||
         f == ClockFace::Moon || f == ClockFace::CalciferCountdown || f == ClockFace::Castalia;
}

uint16_t pm_faces_last_bg565(void) { return s_clock_bg565; }

bool pm_faces_local_hm_changed(int hour, int min) {
  if (s_clock_face == ClockFace::Castalia) {
    return false;
  }
  return s_analog_saved_local_h < 0 || hour != s_analog_saved_local_h || min != s_analog_saved_local_m;
}
"""
    (FACES / "pm_clock.cpp").write_text(clock)

  # --- trim ino ---
    work = source
    for name in sorted(all_names, key=len, reverse=True):
        raw = extract_fn(work, name)
        if raw:
            work = work.replace(raw, "", 1)

    work = re.sub(
        r"enum class ClockFace : uint8_t \{[^}]+\};\s*\n\s*static ClockFace g_clock_face = [^;]+;\s*\n",
        "",
        work,
        count=1,
        flags=re.DOTALL,
    )
    work = re.sub(
        r"/\*\* Spotify transport row[\s\S]*?static bool spotify_hit_transport_bar\([^{]+\{[\s\S]*?\n\}\n\n",
        "",
        work,
        count=1,
    )
    work = re.sub(
        r"static uint16_t g_clock_bg565 = 0;\s*\nstatic int g_analog_saved_local_h = -1;\s*\nstatic int g_analog_saved_local_m = -1;\s*\n",
        "",
        work,
        count=1,
    )
    work = re.sub(r"static constexpr float kPi = [^;]+;\s*\nstatic constexpr float kTwoPi = [^;]+;\s*\n", "", work, count=1)
    work = re.sub(
        r"/\*\* Face background[\s\S]*?static constexpr float k_clock_face_hsv_v = [^;]+;\s*\n",
        "",
        work,
        count=1,
    )
    work = re.sub(
        r"static constexpr int kAnalogCx = LCD_WIDTH / 2;\s*\nstatic constexpr int kAnalogCy = LCD_HEIGHT / 2;\s*\nstatic constexpr int kAnalogR = 138;\s*\nstatic constexpr int kAnalogSecLen = kAnalogR - 10;\s*\n",
        "",
        work,
        count=1,
    )

    if '#include "faces/pm_faces.h"' not in work:
        if '#include "pm_commonplace.h"\n' in work:
            work = work.replace(
                '#include "pm_commonplace.h"\n',
                '#include "pm_commonplace.h"\n#include "faces/pm_faces.h"\n#include "pm_display.h"\n',
            )
        else:
            work = work.replace(
                '#include "pm_zodiac_glyphs.h"\n',
                '#include "pm_zodiac_glyphs.h"\n#include "faces/pm_faces.h"\n#include "pm_display.h"\n',
            )
    if "pm_display_bind" not in work:
        work = work.replace(
            "pm_screen_http_begin(gfx);",
            "pm_display_bind(gfx);\n  pm_screen_http_begin(gfx);",
        )

    repl = [
        ("g_clock_face", "pm_faces_current()"),
        ("cycle_clock_face(", "pm_faces_cycle("),
        ("is_commonplace_home_face()", "pm_faces_is_commonplace_home()"),
        ("draw_clock_face(", "pm_faces_draw("),
        ("draw_astro_voice_screen", "pm_face_astrology_draw_voice_screen"),
        ("draw_voice_wave_screen", "pm_face_draw_voice_wave_screen"),
        ("drawCenteredLine", "pm_face_draw_centered_line"),
        ("draw_astrology_face", "pm_face_astrology_draw"),
        ("draw_circumference_rainbow_24h", "pm_face_draw_circumference_rainbow_24h"),
        ("spotify_hit_transport_bar", "pm_face_spotify_hit_transport_bar"),
        ("build_moon_voice_message", "pm_face_moon_build_voice_message"),
        ("build_moon_system_prompt()", "pm_face_moon_build_system_prompt(g_moon_sys_prompt, sizeof(g_moon_sys_prompt))"),
        ("build_astrology_voice_message", "pm_face_astrology_build_voice_message"),
        (
            "build_astrology_system_prompt()",
            "pm_face_astrology_build_system_prompt(g_astrology_voice_msg, sizeof(g_astrology_voice_msg), "
            "g_astrology_sys_prompt, sizeof(g_astrology_sys_prompt))",
        ),
        (
            "valid && pm_faces_current() != ClockFace::Castalia &&\n          (g_analog_saved_local_h < 0 || "
            "tm_now.tm_hour != g_analog_saved_local_h ||\n           tm_now.tm_min != g_analog_saved_local_m)",
            "valid && pm_faces_local_hm_changed(tm_now.tm_hour, tm_now.tm_min)",
        ),
    ]
    for a, b in repl:
        work = work.replace(a, b)

    if "void setup()" not in work or "void loop()" not in work:
        print("refusing: trimmed ino missing setup/loop", file=sys.stderr)
        return 1
    if len(work.splitlines()) < 400:
        print(f"refusing: trimmed ino too short ({len(work.splitlines())} lines)", file=sys.stderr)
        return 1

    INO.write_text(work)
    print(f"OK: wrote faces/ and trimmed ino ({len(work.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
