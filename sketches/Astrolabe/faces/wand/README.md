# Wand face

Tiny Astrolabe voice pendant: **full-time STT** and a **single faculty conversation** UI.

| Target | Path |
|--------|------|
| M5 AtomS3R + Atomic Voice Base (Echo Base) | [`atom/`](../../atom/) native ESP-IDF |
| Round watch (dev / parity) | `ClockFace::Wand` in this sketch |

Voice pipeline uses `face=wand` (see [`include/astrolabe_wand_face.h`](../../include/astrolabe_wand_face.h)).

On the watch, swipe to **Wand** (`face wand`) for the faculty bust layout; **Atom** hardware runs continuous listen in `atom/main/main.c`.
