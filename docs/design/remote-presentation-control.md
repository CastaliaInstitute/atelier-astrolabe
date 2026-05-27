# Remote presentation control

Astrolabe exposes a disabled-by-default remote command plane for synchronized
multi-device tours.

## Security

Pair each tour host with the device, or set `MYNAH_REMOTE_CONTROL_KEY` in
`include/secrets.local.h` as a lab fallback. When neither a paired key nor a
static key exists, remote commands are rejected.

Pairing requires local physical/serial access:

```text
remote pair 120
```

The device prints a six-digit code and opens `/pair` for the requested number of
seconds. Complete pairing from the tour host:

```bash
./scripts/remote_pair.py astrolabe-abcdef.local --code 123456
```

Or let the helper open the serial pairing window and read the code:

```bash
./scripts/remote_pair.py astrolabe-abcdef.local --port /dev/cu.usbmodem1101
```

The helper prints a per-device token. Put that token into the tour script's
device entry, or pass a shared lab fallback with `ASTROLABE_REMOTE_KEY`.

Use `remote status` to inspect pairing state and `remote clear` to remove the
stored paired key.

WiFi requests must include either:

- `Authorization: Bearer <MYNAH_REMOTE_CONTROL_KEY>`
- `X-Astrolabe-Key: <MYNAH_REMOTE_CONTROL_KEY>`

BLE writes include the same paired/static token as a JSON `token` field.

## WiFi

`POST http://<device>/control` accepts JSON:

```json
{"cmd":"face","face":"moon"}
{"cmd":"button","button":"boot"}
{"cmd":"button","button":"pwr_hold","durationMs":1200}
{"cmd":"tap","x":233,"y":233,"durationMs":120}
{"cmd":"tts","face":"classic","text":"Welcome to the first station."}
{"cmd":"tts","face":"faculty","facultySlug":"a.einstein","facultyName":"Einstein","text":"I am speaking with Castalia faculty TTS."}
{"cmd":"tour","mode":"tts","dwellMs":1200}
{"cmd":"stop"}
```

`GET /control` reports whether control is enabled and the queued sequence.

## Scripted Tours

Use `scripts/remote_tour.py` to run a JSON tour script against one or more
devices:

```bash
ASTROLABE_REMOTE_KEY="shared-secret" \
  ./scripts/remote_tour.py tours/astrolabe-capabilities-tour.example.json
```

The runner supports these step actions:

- `face`: switch a device to a face.
- `tts` or `say`: speak text through the device's TTS path.
  Optional `facultySlug` / `facultyName` selects a Castalia faculty TTS voice
  when the backend has one configured. Direct `ttsVoiceName` is available for
  QA, but product tours should prefer faculty fields.
- `introduce`: speak a concise self-introduction generated from the device
  metadata, including `name`, `role`, `variant`, `platform`, and `homeFace`.
- `button`: trigger `boot`, `pwr`, or `pwr_hold`.
- `tap`, `touch_down`, `touch_up`: inject touchscreen input.
- `swipe`: script-level gesture cue expanded to timed touch commands.
- `stage`: script-only presenter direction.
- `tour`: start the built-in firmware face tour, with `mode` set to `narrate`
  or `tts`.
- `stop`: stop playback/tour/input.
- `wait`: sleep between cues.

Targets can be `all`, a device `id`, a `name`, a `role`, a `group`, or an array
of those values. Text supports simple substitutions such as `$name`, `$role`,
`$group`, and entries from the script `vars` object.

Variant master tour:

```bash
ASTROLABE_REMOTE_KEY="shared-secret" \
  ./scripts/remote_tour.py tours/astrolabe-variant-master-tour.example.json
```

Devices may define `facultySlug` and `facultyName`; `tts` steps inherit those
fields unless the step overrides them.

## BLE

When the BLE host is active, Astrolabe attaches a GATT service:

- Service UUID: `7f5a0001-6d55-4f6a-8fd8-a5701abe0001`
- Command characteristic UUID: `7f5a0002-6d55-4f6a-8fd8-a5701abe0001`

Write the same JSON command payload used by WiFi, plus `token`. The
characteristic read value is updated with a small JSON status response.

Current firmware starts the BLE host on BLE-heavy faces such as Radar and
Biometrics to protect heap for TLS/TTS on other faces. WiFi control is the
always-on path once the device is connected to the LAN.
