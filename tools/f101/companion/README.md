# F101 rear-display face sync

`rear_sync.py` connects the installed `castalia.institute.mynahastrolabe`
rear-display app to the attached Astrolabe over authenticated BLE. It reads the
app's `selected_slug` preference and uses its existing `SELECT_FACE` service
intent. It does not modify the APK, device permissions, or firmware.

Start from the repository root:

```sh
tools/f101/.venv/bin/python tools/f101/companion/rear_sync.py --address A0:F2:62:E4:3F:12
```

The initial connection follows the watch. Subsequent phone selections are sent
to the watch; watch selections are copied to the rear display. Copied selections
are not echoed back. After a disconnect the watch wins again, so offline phone
swipes are not replayed. A new phone selection observed during a write is handled
on the next cycle. If both devices change between observations, the phone wins.

Updates take several seconds with the current connect-per-operation BLE helper.
This is face-selection sync, not pixel mirroring or synchronization of each
face's internal animation/state. Both devices must enable the selected face.
The installed phone app rejected Moon during validation, despite its presence
in the captured catalog. Unavailable faces are reported in
`tools/f101/.state/rear-sync-status.json`; no successful sync is claimed.
Physical swipes on the watch still depend on its touch-controller reliability.

The loop runs until stopped. It does not automatically start after F101 reboots.
Only run one controller at a time during firmware flashing or BLE maintenance.
The process owns `rear-sync.lock` to prevent duplicate sync loops.

Tests:

```sh
tools/f101/.venv/bin/python tools/f101/companion/test_rear_sync.py
```

`sync.py` and `index.html` provide an optional localhost browser diagnostic
carousel on port 8765. This is separate from the rear-display app. Do not run
both controllers concurrently. Its tests are in `test_sync.py`.

## Validation on F101

Five rear-sync state tests passed, covering both directions, echo suppression,
swipes arriving during a write, reconnect behavior, and unknown faces. The
installed rear-display app accepted Pocketwatch and Settings selections, and
BLE returned confirmed Pocketwatch status after a control write. The full
physical two-way test remains incomplete: a subsequent write returned Android
GATT 133, then the watch disappeared from both USB and BLE discovery. Do not
interpret these checks as verified physical swipe synchronization.

The running background process records its PID in `.state/rear-sync.pid`,
logs transitions in `.state/rear-sync.log`, and retries while disconnected.
Android pairing consent is handled only for the specifically addressed control
write; the firmware still checks the shared control key. Other BLE reads and
scans do not approve pairing.
