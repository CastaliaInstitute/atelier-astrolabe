# LVGL Face Port Plan

Goal: move Astrolabe faces from direct `Arduino_GFX` drawing to shared LVGL views, with animated transitions owned by the face shell.

## Current Slice

- Keep all existing `ClockFace` values rendering through the current framebuffer path.
- Add shell-level animated transitions between every face. This works before individual faces are rewritten because it composites complete framebuffers.
- Keep the web simulator selector aligned with the firmware `ClockFace` enum so porting does not shift IDs.
- Keep `ui/astrolabe_ui.*` as the LVGL-first catalog and destination API.

## Target Shape

Each face becomes a small LVGL module with this shape:

```c
typedef struct astrolabe_face_view astrolabe_face_view_t;

astrolabe_face_view_t *astrolabe_face_create(lv_obj_t *parent);
void astrolabe_face_set_context(astrolabe_face_view_t *view, const astrolabe_face_context_t *ctx);
void astrolabe_face_tick(astrolabe_face_view_t *view, uint32_t now_ms);
void astrolabe_face_destroy(astrolabe_face_view_t *view);
```

The shell owns:

- face routing and enum mapping
- enter/leave lifecycle
- transitions between old and new face roots
- gesture dispatch
- device/browser adapter plumbing

Faces own:

- LVGL objects
- local visual state
- face-specific touch/gesture interpretation
- rendering of live data already prepared by services

## Port Order

1. Static/time faces: `ClassicAnalog`, `DigitalLocal`, `Settings`.
2. Ring/dial faces: `Weather`, `CalciferCountdown`, `Apocalypso`, `Rocket`.
3. Data cards: `Notes`, `QuestionOfDay`, `Quotes`, `Faculty`, `InqCard`.
4. Divination/astral: `Moon`, `Tarot`, `Lenormand`, `Runes`, `Geomancy`, `Pythia`, `EnochianAngel`, `Alethiometer`.
5. Motion/presence: `Radar`, `Level`, `Orientation`, `Luopan`, `Sky`, `Globe`, `Biometrics`, `Watcher`.
6. Audio/instrument faces: `Spectrum`, `Tuning`, `Chakra`, `TibetanBowl`, `Ocarina`, `PitchPipe`, `Bongo`, `Piano`, `Kalimba`, `Drone`, `Chord`, `PanDrum`.
7. Voice/USB variants: `Spotify`, `BabelFish`, `HidTouchpad`, `Wand`.

## Compatibility Rule

Until a face is fully LVGL-native, it may remain on the framebuffer path. The shell transition layer should treat both native LVGL and legacy framebuffer faces as routable face surfaces so migration can happen face by face without breaking the dial.
