# Host round simulator (Level 1)

Fastest path for **face design**: 466×466 round viewport, mouse as touch, optional BMP from hardware QA.

```bash
pip install pygame
python3 sim/host_round/viewer.py
python3 sim/host_round/viewer.py artifacts/qa-spotify-2026-05-17.bmp
```

Build flag `MYNAH_SIM_HOST` selects `mynah_hal_host.h` (SDL backend planned under `sim/lvgl_sdl/`).

## Gestures to simulate manually

| Action | Host input |
|--------|------------|
| Swipe up/down | Drag vertically |
| Tap | Click |
| Long press | Hold mouse button |
| Two-finger | Not yet — extend `TouchMouse` |

Faces should not depend on this module; only `mynah::Display` / `mynah::Touch`.

## CI

```bash
./scripts/ci-sim.sh
```

Runs `test_sim_ci.py` (headless) on every PR to `integration` / `main`.
