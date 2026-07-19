# Codex Desktop Remote pairing PWA

The Astrolabe Cyber firmware serves a pairing launcher at:

```text
http://astrolabe-xxxx.local/codex
```

The page runs on the phone but is served directly by Astrolabe. It scans the QR
code displayed by Codex Desktop Remote Control, validates that the destination
belongs to ChatGPT or OpenAI, and opens the official ChatGPT setup flow.

## Pair a phone

1. Put the phone and Astrolabe on the same Wi-Fi network.
2. In Codex Desktop on the Mac, choose **Set up Remote**.
3. Open `/codex` from Astrolabe's home page on the phone.
4. Choose **Scan desktop QR** and point the phone at the Mac.
5. If the browser blocks live camera access on the local HTTP origin, choose
   **Choose QR photo** or paste the pairing link.
6. Choose **Continue in ChatGPT** and complete the account, workspace, MFA,
   SSO, or passkey checks required by ChatGPT.

Astrolabe does not save the scanned link. Connected phones remain managed in
Codex Desktop under **Settings > Connections** and in the official ChatGPT
Remote client.

## Connect task control

The QR flow and Astrolabe task control are deliberately separate. The QR pairs
the phone with OpenAI's official Remote service. On every Mac that should appear
in the Astrolabe task list, run the local companion against the device:

```sh
python3 scripts/astrolabe_codex_companion.py \
  --astrolabe http://astrolabe-xxxx.local \
  --name "Studio Mac"
```

The companion starts the installed `codex app-server`, which uses that
machine's existing Codex authentication. It sends only bounded thread metadata
to Astrolabe: task ID, title, status, update time, and machine name. It never
sends an OpenAI credential, ChatGPT cookie, or Remote pairing secret. Run one
companion per machine; host snapshots are merged by stable host ID and the
firmware supports four machines and twelve visible tasks.

For each new task title, the companion asks the locally authenticated Codex
model to choose one item from a fixed 32-icon task catalog. Choices are cached
in `~/.cache/astrolabe-codex/task-emoji.json`, so polling does not repeatedly
invoke the model. Astrolabe receives only the selected numeric index and renders
the matching embedded Google Noto Color Emoji ARGB sprite. Use
`--no-ai-emoji` for an offline robot-icon fallback.

The `/codex` PWA and the Codex face show the same selected task and persistent
pinned-first order. The physical face presents tasks as a circumference dial
with twelve fixed segments. Pinned tasks fill the first available segments, so
the ring remains stable as task lists change; empty segments stay dim as quiet
capacity markers. Each emoji has a status-colored silhouette halo: blue for idle/input, green for
running/done, yellow for approval, and red for error. Pinned tasks are larger and
the selected task has a brighter pulsing halo. Tap any icon to select and open
that task directly. Swipe up/down advances the dial highlight, and long-tap an
icon to pin or unpin it. The side button approves a waiting request, interrupts
a running task, or queues voice input while idle.

Long-pressing a task opens its task controls face. Tap the model, context, or
speed row on the left/right side (or swipe that row up/down) to cycle choices;
long-press again to return to the dial.

The pinned-task ring also visualizes usage: segment color progresses from blue
through green, yellow, orange, and red as cumulative token spend crosses
logarithmic thresholds. A task with a nonzero recent token rate flashes its
segment; higher rates flash faster.

The PWA provides a star control on every task row so any task can be pinned or
unpinned directly without selecting it first. The emoji button on each row opens
the complete task-icon catalog; a manual choice is stored on Astrolabe and wins
over companion syncs until **Use AI choice** clears the override. The PWA can
also select tasks, submit a text prompt, interrupt a turn, and explicitly approve
a command or file change owned by its companion. Approval is never automatic.

## Security boundary

The Desktop QR is an entry point into the official authenticated pairing flow;
it is not a documented third-party Remote API credential. The PWA therefore
does not decode or replay private protocol fields. It accepts only:

- HTTPS destinations on `chatgpt.com`, `openai.com`, or their subdomains
- `chatgpt:` and `codex:` application links

The page uses a restrictive content security policy, sends no pairing payload
to an Astrolabe API, and discards the camera stream before opening ChatGPT. The
device API is same-origin and rejects browser-simple writes by requiring the
`X-Astrolabe-Codex: sync-v1` header. It is intended for a trusted local network;
that header is a CSRF boundary, not user authentication. Do not expose the
Astrolabe HTTP server directly to the Internet.

The local companion is a separate Codex client, not an undocumented clone of
the proprietary Desktop Remote relay. Consequently, it can list the shared
local thread store, resume tasks, start prompts, and control turns that it owns,
but it cannot take ownership of an approval callback already held by another
Codex Desktop process. Such an approval remains queued for the owning client.

## Device protocol

- `GET /api/codex/state` returns hosts, pinned-first tasks, selection, and the
  pending action sequence. Each task also carries cumulative local session
  token usage (`inputTokens`, `cachedInputTokens`, `outputTokens`,
  `reasoningOutputTokens`, and `totalTokens`) plus `rateTokensPerMinute`, an
  observed recent rate calculated from token-count events. It is not a billing
  or provider quota rate; it is zero when the local session has no usable data.
- `POST /api/codex/state` accepts a bounded snapshot. `replaceHostId` replaces
  only one companion's contribution, enabling multiple machines.
- `POST /api/codex/action` changes selection, pins, or persistent task emoji and
  queues `open`, `prompt`, `interrupt`, `approve`, or `voice` for the owning
  companion. `emoji-set` accepts a catalog index or `-1` to restore AI choice.
- A snapshot acknowledges a completed action with `ackActionSequence`. Until
  then, the action stays visible instead of being silently discarded.

## Browser behavior

Live `getUserMedia()` camera capture normally requires a secure context. Since
the embedded server currently uses local HTTP, availability depends on the
browser. The page provides photo selection and pasted-link fallbacks and uses
the browser's `BarcodeDetector` when present. No QR-decoding code is loaded
from a third-party CDN.
