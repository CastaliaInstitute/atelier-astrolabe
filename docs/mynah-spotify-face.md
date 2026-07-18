# Design Document: Mynah Spotify Face

**“Vinyl Queue”** — Browse Without Playing, Tap to Commit

## 1. Purpose

The Mynah Spotify Face is a music-control and music-browsing face for the Mynah device family. It presents Spotify playback as a tactile, album-art-driven vertical stream: the current or selected track appears as a spinning vinyl record in the center, future tracks appear above, and previous/recent tracks appear below.

The key interaction principle is:

**Swipe explores. Tap commits.**

Swiping up and down should browse the musical stream without changing playback. The user must tap the selected track to play it. This avoids accidental skips and makes the face feel calm, deliberate, and suitable for a shared home device.

---

## 2. Product Goals

### Primary goals

1. Make music feel physical, beautiful, and room-aware.
2. Use album art as the dominant visual material.
3. Allow queue browsing without disrupting playback.
4. Support simple controls on a small round screen.
5. Integrate with Mynah’s broader house, calendar, mindfulness, and Commonplace systems.

### Secondary goals

1. Use Mynah hue-time context as an ambient layer.
2. Let users save meaningful music moments to Commonplace.
3. Support future lesson/practice modes.
4. Support room-based Spotify endpoints.
5. Degrade gracefully when Spotify data is incomplete.

---

## 3. Core UX Concept

The face is a vertical music stream.

```
          Future / next tracks
                  ↑
             swipe up/down
             selected track
          tap center to play
                  ↓
          Past / previous tracks
```

The center object is not always the currently playing track. It is the **selected track**.

The system maintains two independent concepts:

| Concept | Meaning |
|---------|---------|
| `playing_track` | The track Spotify is actually playing |
| `selected_track` | The track currently centered on the face |

When `selected_track == playing_track`, the face is in **Now Playing** mode.

When `selected_track != playing_track`, the face is in **Browse** mode.

---

## 4. Interaction Rules

### 4.1 Tap

Tap behavior depends on what is selected.

| Selected track | Tap action |
|----------------|------------|
| Selected track is currently playing | Pause/resume |
| Selected track is not currently playing | Play selected track |
| Nothing is playing | Play selected track or resume last context |
| Spotify unavailable | Show connection prompt |

**Rule:**

```
If selected == playing:
    tap = pause/resume
else:
    tap = play selected
```

### 4.2 Swipe up

Swipe up browses forward. It should not immediately play anything.

Possible sources: upcoming queue, playlist continuation, album continuation, recommendations, scheduled Mynah music.

### 4.3 Swipe down

Swipe down browses backward. It should not immediately play anything.

Possible sources: recently played tracks, earlier playlist tracks, earlier album tracks, past scheduled music.

### 4.4 Long press

Long press opens secondary controls: Volume, Device, Shuffle, Repeat, Like, Add to Commonplace, Open Playlist, Return to Now.

### 4.5 Double tap

Double tap returns selection to the currently playing track: `selected_track = playing_track`.

### 4.6 Horizontal swipe

Horizontal swipe should remain reserved for changing Mynah faces.

---

## 5. Visual Design

### 5.1 Overall composition

The screen is circular. The main layout is vertically oriented:

```
        next +2, small/faded
          next +1
        ┌───────────┐
        │ selected  │
        │  record   │
        └───────────┘
        previous -1
      previous -2, small/faded
```

For the round display, the top and bottom track labels may be clipped by the circular edge. This is acceptable and can feel intentional.

### 5.2 Center record

The center selected track appears as a record. Album art is the primary visual material.

Recommended record treatment: dark vinyl base, album art clipped to circular disc, subtle radial darkening, vinyl groove overlay, small spindle dot, optional tonearm, progress arc if currently playing.

| Option | Description |
|--------|-------------|
| A — Album art as center label | Most readable; dark vinyl with art as center label |
| B — Album art as full record texture | Most distinctive; art fills whole disc |
| C — Hybrid | Art fills record + vinyl overlay, grooves, radial shadow, spindle (**recommended**) |

---

## 6. Mode-Specific Visual Behavior

### 6.1 Now Playing

`selected_track == playing_track`, music playing: center record spins, progress arc, “Now Playing” or speaker icon, album art glow, queue dimmed, tap pauses/resumes.

### 6.2 Paused

`selected_track == playing_track`, `is_playing == false`: record stopped, progress arc remains, play icon at center, reduced glow, tap resumes.

### 6.3 Browse mode

`selected_track != playing_track`: selected record still, no progress arc (or faint preview), “Tap to play”, mini now-playing marker, browsing does not alter Spotify playback.

### 6.4 No music playing

Dormant vinyl, ghosted last art, “Tap to resume” or “Choose music”, hue-time ring visible.

### 6.5 Spotify unavailable

“Spotify unavailable / Connect from phone”; optional Wi‑Fi/Spotify icons, dim cached artwork.

---

## 7. Queue and Browsing Model

The face should not depend entirely on Spotify’s native queue. Mynah constructs a **Music Stream**.

### 7.1 Music Stream

```json
{
  "selected_index": 12,
  "playing_index": 10,
  "items": [
    {
      "id": "spotify:track:...",
      "title": "Track Title",
      "artist": "Artist Name",
      "album": "Album Name",
      "album_art_url": "https://...",
      "duration_ms": 218000,
      "source": "queue",
      "is_playable": true,
      "is_current": false
    }
  ]
}
```

### 7.2 Stream sources

1. Spotify currently playing track  
2. Spotify queue (when available)  
3. Recently played tracks  
4. Playlist or album context  
5. Mynah scheduled music  
6. Recommendations  
7. Local Castalia music curriculum sequence  

### 7.3 Directionality

Above center = forward / future; below center = backward / past (relative to selected item while browsing).

---

## 8. State Machine

### 8.1 Main states

`NO_MUSIC` | `NOW_PLAYING` | `PAUSED` | `BROWSING_WHILE_PLAYING` | `BROWSING_WHILE_PAUSED` | `SPOTIFY_UNAVAILABLE` | `LOADING`

### 8.2 State transitions (summary)

| From | Event | To |
|------|-------|-----|
| NOW_PLAYING | swipe | BROWSING_WHILE_PLAYING |
| NOW_PLAYING | tap | PAUSED |
| PAUSED | swipe | BROWSING_WHILE_PAUSED |
| PAUSED | tap | NOW_PLAYING |
| BROWSING_* | tap selected | NOW_PLAYING (selected track) |
| BROWSING_* | double tap / timeout | NOW_PLAYING or PAUSED (current track) |

---

## 9. Timeout Behavior

After **15 seconds** of no interaction in browse mode, selection returns to currently playing track (gentle animation). Configurable: `BROWSE_TIMEOUT_MS` (15000); future setting `browse_timeout`: never | 10s | 15s | 30s | 60s.

---

## 10. Animation Design

- **Spin** only when Spotify is actually playing (8–15 s/revolution; 20–60 s in docked ambient mode).
- Browse: selected record still unless it is the playing track.
- Tap non-current track: mini-marker fades, glow, hub play command, spin + progress arc on confirm.
- Pause: decelerate 200–350 ms, play icon, dim glow, optional tonearm lift.

---

## 11. Progress Indicator

Thin circular progress arc around selected record **only** when selected track is the playing track.

| State | Progress ring |
|-------|----------------|
| Playing selected | Active progress arc |
| Paused selected | Static progress arc |
| Browsing another | None or faint neutral ring |
| Loading | Indeterminate orbit dot |

---

## 12. Hue-Time Integration

Layer model (back to front): blurred album art background → dark vignette → Mynah hue-time ring → queue text → center record → progress ring → state icons.

Avoid hardcoding Spotify green as the main color; Mynah-first, Spotify-second.

---

## 13. Typography

Minimal text per queue item: track title + artist (or `Title · Artist` on small displays). Scale: selected largest 100%; adjacent 65–75%; second adjacent 30–40% opacity.

---

## 14. Mynah-Specific Features

- **Commonplace capture** (long press): `music_listening_event` with track, artist, album, URI, timestamp, room, device, context, mood_color.
- **Room awareness**: subtle room label when acting as Spotify Connect endpoint.
- **Music practice mode**: curriculum-aware stream (future).
- **Ritual / mindfulness mode**: minimal queue text, breathing pulse (future).

---

## 15. System Architecture

ESP32 handles rendering, gestures, animation, cached assets, local state. Hub / phone / server handles OAuth, polling, queue/recent, album art pipeline, playback commands, Commonplace logging, room coordination.

```
ESP32 Mynah device
        ⇅
local WebSocket / MQTT / BLE
        ⇅
Mynah Hub / phone / local server
        ⇅
Spotify Web API + Spotify Connect
```

Astrolabe today: the native face lives in [`faculty175_face_spotify.c`](../astrolabe175c/main/faculty175_face_spotify.c). Vinyl Queue replaces the earlier transport-bar UI/gesture model; the hub stream payload remains the v1 target contract.

---

## 16. Data Payloads

### 16.1 Playback state (`spotify_state`)

```json
{
  "type": "spotify_state",
  "timestamp": 1779040800000,
  "device": { "id": "device_id", "name": "Parlor Mynah", "volume": 62, "is_active": true },
  "playback": { "is_playing": true, "progress_ms": 94000, "shuffle": false, "repeat": "off" },
  "playing_index": 10,
  "selected_index": 10,
  "items": [
    {
      "id": "spotify:track:abc",
      "title": "Track Title",
      "artist": "Artist Name",
      "album": "Album Name",
      "duration_ms": 218000,
      "album_art_asset": "asset://album_abc_240.rgb565",
      "dominant_color": "#5B2E22",
      "highlight_color": "#E6A85C",
      "text_color": "#F3E7D5",
      "source": "queue",
      "is_playable": true
    }
  ]
}
```

### 16.2 Commands from device

| Action | Payload |
|--------|---------|
| Toggle play/pause | `{ "type": "spotify_command", "command": "toggle_play_pause" }` |
| Play track | `{ "type": "spotify_command", "command": "play_track", "track_uri": "...", "context_uri": "..." }` |
| Browse | `{ "type": "ui_command", "command": "browse_delta", "delta": 1 }` |
| Return to now | `{ "type": "ui_command", "command": "return_to_now" }` |
| Commonplace | `{ "type": "commonplace_command", "command": "save_music_event", "track_uri": "..." }` |

---

## 17. Album Art Processing

Hub pipeline: fetch → square crop → resize → circular mask → blurred background → palette → RGB565 → cache → asset reference to device.

Suggested per track: `album_disc_240.rgb565`, `album_bg_480.rgb565`, `palette.json` (lower-memory: disc 160 + dominant color only).

---

## 18. Rendering Model

LVGL-style object tree: `spotify_face` → bg blur/vignette → hue ring → queue labels → `record_group` (disc, art, grooves, spindle, icons) → progress arc → now marker → room label.

Priorities: high = record, play state, selected title, gestures; medium = queue, progress, now marker; low = full blur bg, tonearm, beat pulse.

---

## 19. Performance Strategy (v1)

Do not rotate full album art at high FPS. Static or slow art + rotating groove/glint overlay; progress arc ~1 Hz; low-FPS transitions. Optional hub pre-rendered frames (tradeoff: storage vs CPU).

**v1 compromise:** static album disc + rotating glint/groove + progress arc + subtle glow pulse.

---

## 20. Memory Considerations

Avoid multiple full-screen buffers in RAM. v1 profile: center disc ~200×200 RGB565 (~80 KB), optional bg 240×240 (~115 KB), small groove mask, no queue thumbnails in v1.

---

## 21. API Behavior and Spotify Limitations

Queue may be incomplete; recently played may not match context; commands may fail without active device; Connect state lags; album art needs caching. Hub presents stable **Mynah Music Stream** abstraction to firmware.

---

## 22. Error Handling

- Playback fails → “Couldn’t play / Try from phone”, return to browse.
- No active device → simple “Open Spotify and choose Mynah” (v1).
- No art → hash gradient or generic vinyl.
- Offline → “Offline / Last music shown”.

---

## 23. Accessibility and Usability

- Swipes never change playback; only tap commits.
- Clear browse vs playing cues (spin, arc, copy, glow).
- Sparse text on 1.75″ display.
- Shared-home safety: intentional actions only.

---

## 24. V1 Scope

**Required:** center album-art record; tap pause/resume current; swipe browse without playback change; tap browsed track to play; browse timeout to now; now marker while browsing; queue labels; progress arc; hub `spotify_state`; hub album art preprocess.

**Nice:** glint overlay; album-derived bg; hue-time ring; Commonplace save; room label.

**Excluded:** lyrics, beat sync, full queue editor, ESP32 OAuth, high-FPS art rotation, complex device picker, browse previews.

---

## 25. V2 Scope

Device picker, radial volume, playlist browser, like/save, voice note to Commonplace, lesson mode, lyrics, multiroom, recommendations, pre-rendered frames, tonearm, mini-disc now marker, docked ambient screensaver.

---

## 26. Suggested User Flows

1. **Pause:** spinning record → tap → stop + play icon.  
2. **Browse without interrupt:** swipe up → still center + “Tap to play” → music continues → timeout returns to now.  
3. **Play browsed:** swipe → tap → switch playback + spin.  
4. **Return to now:** double tap → selection snaps to playing track.  
5. **Commonplace:** long press → Save to Commonplace.

---

## 27. Pseudocode

```c
typedef enum {
    SPOTIFY_NO_MUSIC,
    SPOTIFY_NOW_PLAYING,
    SPOTIFY_PAUSED,
    SPOTIFY_BROWSING_WHILE_PLAYING,
    SPOTIFY_BROWSING_WHILE_PAUSED,
    SPOTIFY_UNAVAILABLE,
    SPOTIFY_LOADING
} spotify_face_mode_t;

typedef struct {
    int selected_index;
    int playing_index;
    bool is_playing;
    uint32_t last_browse_ms;
    spotify_face_mode_t mode;
} spotify_face_state_t;

#define BROWSE_TIMEOUT_MS 15000

void on_swipe_up() {
    state.selected_index += 1;
    state.last_browse_ms = millis();
    state.mode = state.is_playing ? SPOTIFY_BROWSING_WHILE_PLAYING : SPOTIFY_BROWSING_WHILE_PAUSED;
    render_selected_track();
}

void on_tap_center() {
    if (state.selected_index == state.playing_index)
        send_command_toggle_play_pause();
    else
        send_command_play_track(state.selected_index);
}

void on_double_tap() {
    state.selected_index = state.playing_index;
    state.mode = state.is_playing ? SPOTIFY_NOW_PLAYING : SPOTIFY_PAUSED;
    render_selected_track();
}

void update_browse_timeout() {
    if (is_browsing_mode(state.mode) &&
        millis() - state.last_browse_ms > BROWSE_TIMEOUT_MS) {
        state.selected_index = state.playing_index;
        state.mode = state.is_playing ? SPOTIFY_NOW_PLAYING : SPOTIFY_PAUSED;
        render_selected_track();
    }
}
```

---

## 28. Implementation Milestones

| Milestone | Deliverable |
|-----------|-------------|
| **M1** | Static mock face: layout, fake art, selected vs now-playing distinction |
| **M2** | Gesture prototype: tap, swipe, double-tap, browse timeout |
| **M3** | Hub integration: `spotify_state` render, cached art, commands to hub |
| **M4** | Spotify: auth, playback, Music Stream assembly (mynah hub) |
| **M5** | Polish: hue ring, glint, Commonplace, ambient dock mode |

---

## 29. Open Design Questions

1. Swipe direction inverted? — **Proposal:** up = forward/upcoming.  
2. Second tap to confirm play? — **Proposal:** one tap plays.  
3. Browse timeout only when playing? — **Proposal:** always 15s.  
4. Now marker v1 text vs v2 mini-disc? — **v1 text.**  
5. Spotify previews while browsing? — **No.**  
6. Spin when paused selected? — **No; spin = active playback.**

---

## 30. Final Recommendation

Build around **`selected_track ≠ playing_track`**. Swipes change selection only; taps commit. Currently playing remains marked while browsing; auto-return to Now Playing after timeout.

> Swipe through musical possibility. Tap to let one song enter the room.

---

## Related code (astrolabe)

| Path | Role |
|------|------|
| [`astrolabe175c/main/faculty175_face_spotify.c`](../astrolabe175c/main/faculty175_face_spotify.c) | Native round-watch Spotify face |
| Castalia `mynah-spotify` service | Spotify state and command backend |
| [`docs/BACKLOG.md`](BACKLOG.md) | Backlog epic — Mynah Spotify Face |
