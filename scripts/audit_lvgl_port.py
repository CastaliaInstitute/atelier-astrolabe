#!/usr/bin/env python3
"""Audit core LVGL face-port and navigation responsiveness invariants."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FACES_H = ROOT / "astrolabe175c" / "main" / "faculty175_faces.h"
LVGL_C = ROOT / "astrolabe175c" / "main" / "faculty175_lvgl.c"
FACE_DISPATCH_C = ROOT / "astrolabe175c" / "main" / "faculty175_face_dispatch.c"
MAIN_C = ROOT / "astrolabe175c" / "main" / "main.c"
GESTURE_C = ROOT / "astrolabe175c" / "main" / "faculty175_gesture.c"
MAGNET_RGB565 = ROOT / "astrolabe175c" / "storage_seed" / "space" / "magnetosphere_466.rgb565"
MAGNET_REFRESH = ROOT / "scripts" / "refresh_magnetosphere_map.py"
TAROT_DOCS_466 = ROOT / "docs" / "assets" / "deck" / "466"
TAROT_SPIFFS_466 = ROOT / "astrolabe175c" / "storage_seed" / "tarot" / "deck" / "466"
TAROT_SPIFFS_CPP = ROOT / "astrolabe175c" / "main" / "faculty175_face_tarot_spiffs_image.cpp"


def face_tokens() -> list[str]:
    text = FACES_H.read_text()
    enum_body = text.split("FACULTY175_FACE_COUNT", 1)[0]
    faces: list[str] = []
    for token in re.findall(r"FACULTY175_FACE_[A-Z0-9_]+", enum_body):
        if token not in faces:
            faces.append(token)
    return faces


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def function_body(text: str, name: str) -> str:
    match = re.search(rf"static\s+(?:bool|void|const char \*)\s+{name}\b[\s\S]*?\n}}", text)
    return match.group(0) if match else ""


def main() -> None:
    faces = face_tokens()
    lvgl = LVGL_C.read_text()
    face_dispatch = FACE_DISPATCH_C.read_text()
    main_c = MAIN_C.read_text()
    gesture_c = GESTURE_C.read_text()
    magnet_refresh = MAGNET_REFRESH.read_text() if MAGNET_REFRESH.exists() else ""
    tarot_spiffs = TAROT_SPIFFS_CPP.read_text() if TAROT_SPIFFS_CPP.exists() else ""
    combined = lvgl + "\n" + main_c

    dispatch_draw = re.search(
        r"bool faculty175_face_dispatch_draw[\s\S]*?\n}\n\nbool faculty175_face_dispatch_action",
        face_dispatch,
    )
    dispatch_draw_text = dispatch_draw.group(0) if dispatch_draw else ""
    render_routes = lvgl + "\n" + dispatch_draw_text
    missing_cases = [face for face in faces if re.search(rf"case\s+{face}\s*:", render_routes) is None]
    if missing_cases:
        fail("registered faces missing LVGL case coverage: " + ", ".join(missing_cases))

    instrument_body = function_body(lvgl, "instrument_face_id")
    oracle_body = function_body(lvgl, "oracle_face_id")
    utility_body = function_body(lvgl, "utility_face_id")
    draw_face = re.search(r"bool faculty175_lvgl_draw_face[\s\S]*?\n}\n\nstatic lv_obj_t \*face_screen_for_id", lvgl)
    draw_face_text = draw_face.group(0) if draw_face else ""
    draw_utility = re.search(r"static bool draw_utility\([\s\S]*?\n}\n\nstatic float aleth_symbol_angle", lvgl)
    draw_utility_text = draw_utility.group(0) if draw_utility else ""
    descriptor_fallback_faces = []
    utility_fallback_faces = []
    for face in faces:
        in_instrument = face in instrument_body
        in_oracle = face in oracle_body
        in_utility = face in utility_body
        if in_utility and face not in draw_utility_text:
            utility_fallback_faces.append(face)
        if (not in_instrument and not in_oracle and not in_utility
                and face not in draw_face_text and face not in dispatch_draw_text):
            descriptor_fallback_faces.append(face)
    if descriptor_fallback_faces:
        fail("registered faces route to generated descriptor fallback: " + ", ".join(descriptor_fallback_faces))
    if utility_fallback_faces:
        fail("registered utility faces route to generic utility fallback: " + ", ".join(utility_fallback_faces))

    if "preview-snap" in combined:
        fail("legacy preview-snap navigation metric is still present")
    if "anim=native-nav-slide" not in main_c:
        fail("nav-mode swipe path is not logging anim=native-nav-slide")
    if "animate_face_slide(from_face, face, vertical, delta, now_ms)" not in main_c or "native-snapshot-slide" not in main_c:
        fail("direct face swipe path is not using native snapshot slide transitions")
    if "snapshot-slide-settle" in main_c:
        fail("snapshot slide still performs a redundant full-frame settle flush")
    if "queue_age_ms = gesture.queued_ms" not in main_c:
        fail("navigation metrics do not compute gesture queue age")
    for metric in ("enter face=%s queue_age_ms=%u",
                   "swipe axis=horizontal from=%s to=%s delta=%d queue_age_ms=%u",
                   "swipe axis=vertical from=%s to=%s delta=%d queue_age_ms=%u",
                   "direct-swipe from=%s to=%s delta=%d queue_age_ms=%u",
                   "vertical-direct-swipe from=%s to=%s delta=%d queue_age_ms=%u"):
        if metric not in main_c:
            fail(f"navigation metrics missing queue age field for {metric.split()[0]}")
    transition_face = re.search(r"bool faculty175_lvgl_transition_face[\s\S]*?\n}", lvgl)
    if transition_face and "faculty175_display_flush_suspended_set(true)" not in transition_face.group(0):
        fail("direct LVGL face transition does not suppress destination preload flush")
    if "direct-lvgl-swap" not in main_c:
        fail("direct face swipe path does not retain same-screen direct redraw metrics")
    if "static void draw_utility_weather" not in lvgl:
        fail("weather face does not have a dedicated LVGL utility renderer")
    if "id == FACULTY175_FACE_RADAR || id == FACULTY175_FACE_WEATHER || id == FACULTY175_FACE_GLOBE" in lvgl:
        fail("weather face is still routed through the shared radarish renderer")
    if "static void draw_utility_calcifer" not in lvgl:
        fail("calcifer face does not have a dedicated LVGL utility renderer")
    if 'lv_label_set_text(s_utility_status, "HEARTH")' in lvgl:
        fail("calcifer face still uses the old labeled generic utility treatment")
    if "static void draw_utility_castalia" not in lvgl:
        fail("castalia face does not have a dedicated LVGL utility renderer")
    if 'lv_label_set_text(s_utility_status, "SYSTEM SIGNAL")' in lvgl:
        fail("castalia face still uses the old labeled generic utility treatment")
    if "static void draw_utility_babel" not in lvgl:
        fail("babel face does not have a dedicated LVGL utility renderer")
    if 'lv_label_set_text(s_utility_status, "TRANSLATE")' in lvgl:
        fail("babel face still uses the old labeled generic utility treatment")
    for name in ("notes", "quotes", "qday", "spotify", "rocket", "focus", "biometrics", "hid", "settings",
                 "watcher", "deathstar", "apocalypso"):
        if f"static void draw_utility_{name}" not in lvgl:
            fail(f"{name} face does not have a dedicated LVGL utility renderer")
    for label in ("COMMONPLACE", "COLLECTED", "DAILY QUESTION", "NOW PLAYING", "LAUNCH VECTOR", "FOCUS TIMER",
                  "BODY STATE", "TOUCHPAD", "DEVICE", "DEVICE WATCH", "TARGET", "STORM DIAL"):
        if f'lv_label_set_text(s_utility_status, "{label}")' in lvgl:
            fail(f"utility face cluster still uses old status label {label}")
    generic = re.search(r"static void draw_utility_generic[\s\S]*?\nstatic bool draw_utility", lvgl)
    generic_text = generic.group(0) if generic else ""
    for face in ("SPOTIFY", "ROCKET", "FOCUS", "BIOMETRICS", "HID", "SETTINGS", "WATCHER", "DEATHSTAR", "APOCALYPSO"):
        if re.search(rf"case\s+FACULTY175_FACE_{face}\s*:", generic_text):
            fail(f"{face.lower()} still has a stale branch in draw_utility_generic")
    if "static const faculty175_native_face_t k_faces[]" in lvgl:
        fail("draw_face_descriptor still contains stale face-specific descriptor fallback table")
    if "kCardW = 466" not in tarot_spiffs or "tarot/deck/466" not in tarot_spiffs:
        fail("tarot SPIFFS loader is not using the 466 card deck")
    docs_tarot = sorted(TAROT_DOCS_466.glob("*.png"))
    spiffs_tarot = sorted(TAROT_SPIFFS_466.glob("*.png"))
    if len(docs_tarot) != 78 or len(spiffs_tarot) != 78:
        fail("466 tarot deck is missing cards in docs assets or SPIFFS seed")
    if "w == FACULTY175_LCD_W && h == FACULTY175_LCD_H" not in lvgl:
        fail("tarot LVGL face is not requiring full-screen 466 card art")
    if "s_watch_rendered_day_s" not in lvgl or "day_s == s_watch_rendered_day_s" not in lvgl:
        fail("watch face does not hold hand geometry stable between whole-second ticks")
    if "watch-metrics second=%02u boundary_late_ms=%d update_us=%u step_s=%u" not in lvgl:
        fail("watch face does not log second-hand tick metrics")
    if "nav-anim-metrics kind=%s axis=%s delta=%d expected_ms=%u actual_ms=%u frames=%u avg_gap_ms=%u max_gap_ms=%u overrun_ms=%d" not in lvgl:
        fail("LVGL navigation animations do not log frame cadence metrics")
    for kind in ("nav-preview", "snapshot-slide", "screen-slide"):
        if f'"{kind}"' not in lvgl:
            fail(f"LVGL navigation metrics missing {kind}")
    if "draw_magnetosphere" not in lvgl or "/bust_cache/space/magnetosphere_466.rgb565" not in lvgl:
        fail("magnetosphere face is not backed by the SPIFFS map asset")
    if "magnet_map_derive_field_model" not in lvgl or "s_magnet_data_pressure" not in lvgl:
        fail("magnetosphere animation is not derived from the loaded SWMF map")
    expected_magnet_bytes = 466 * 466 * 2
    if not MAGNET_RGB565.exists() or MAGNET_RGB565.stat().st_size != expected_magnet_bytes:
        fail("SPIFFS magnetosphere RGB565 asset is missing or has the wrong size")
    if "SWMF2023-RT" not in magnet_refresh or "MagnetopausePosition" not in magnet_refresh:
        fail("magnetosphere refresh script is not using the current NASA CCMC SWMF2023 data tree")
    if "NAV_TRANSITION_MS 160" not in main_c:
        fail("nav transition duration is not pinned at 160ms")
    if "#define FACE_CAROUSEL_FRAMES 4" not in main_c or "#define FACE_CAROUSEL_FRAME_MS 16" not in main_c:
        fail("native snapshot carousel is not using the bounded four-frame transition budget")
    if "vTaskDelay(pdMS_TO_TICKS(12))" in main_c:
        fail("manual face transition still has a hardcoded 12ms delay")
    transition_nav = re.search(r"bool faculty175_lvgl_transition_nav[\s\S]*?\n}", lvgl)
    transition_nav_text = transition_nav.group(0) if transition_nav else ""
    if "duration_ms > 0 ? duration_ms : 72" not in transition_nav_text:
        fail("LVGL nav transition fallback duration is not 72ms")
    if "const uint32_t step_ms = 16" not in transition_nav_text:
        fail("LVGL nav transition is not serviced in 16ms steps")
    if "animate_nav_preview_native(false, delta, NAV_TRANSITION_MS)" not in main_c:
        fail("horizontal nav-mode swipes are not using pinned native nav duration")
    if "animate_nav_preview_native(true, delta, NAV_TRANSITION_MS)" not in main_c:
        fail("vertical nav-mode swipes are not using pinned native nav duration")
    if transition_face and "duration_ms > 0 ? duration_ms : 72" not in transition_face.group(0):
        fail("LVGL direct face transition fallback duration is not 72ms")
    if transition_face and "elapsed += 8" not in transition_face.group(0):
        fail("LVGL direct face transition is not serviced in 8ms steps")
    animate_frames = re.search(r"bool faculty175_lvgl_animate_frames[\s\S]*?\n}", lvgl)
    animate_frames_text = animate_frames.group(0) if animate_frames else ""
    if "duration_ms > 0 ? duration_ms : 72" not in animate_frames_text:
        fail("LVGL snapshot animation fallback duration is not 72ms")
    if "elapsed += 16" not in animate_frames_text or "lvgl_tick(16)" not in animate_frames_text:
        fail("LVGL snapshot animation is not serviced in 16ms steps")
    if "vertical-direct-swipe" not in main_c:
        fail("vertical face-group swipe is not using the direct LVGL transition path")
    if "change_face_group_for_vertical" in main_c:
        fail("vertical face-group swipe still uses the old snapshot callback path")
    if "drop_queued_navigation_steps" not in gesture_c:
        fail("gesture queue does not collapse queued navigation steps")
    if "drop stale" not in gesture_c or "GESTURE_NAV_MAX_AGE_MS" not in gesture_c:
        fail("gesture queue does not reject stale navigation input")

    print(f"OK: {len(faces)} registered faces have LVGL case coverage")
    print("OK: nav-mode swipes use native-nav-slide")
    print("OK: direct swipes use native-snapshot-slide without a redundant settle flush")
    print("OK: preview-snap path absent")
    print("OK: stale/queued navigation gestures are bounded")
    print("OK: registered faces avoid descriptor/generic utility fallbacks")
    print("OK: tarot face uses docs/assets/deck/466 and SPIFFS 466 card art")
    print("OK: watch hand geometry is stable between whole-second ticks")
    print("OK: watch second-hand tick metrics are logged")
    print("OK: LVGL navigation animation frame metrics are logged")
    print("OK: magnetosphere face uses a SPIFFS SWMF2023 map asset")


if __name__ == "__main__":
    main()
