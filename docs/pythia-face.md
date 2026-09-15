# Pythia face (185B): a local LLM served from the watch

Select the **Pythia** face and the watch becomes a tiny web host for a local
LLM. It serves `http://astrolabe.local/pythia/` over **USB NCM** (preferred)
and Wi-Fi; the page that loads runs **Qwen2.5-1.5B-Instruct** in the visitor's
browser with [wllama](https://github.com/ngxson/wllama) (llama.cpp compiled to
WebAssembly). The ESP32-S3 never runs the model — a 1.5B model needs ~1 GB of
weights against 8 MB of PSRAM — it streams the page, the WASM runtime and the
GGUF from its SD card, and shows the last exchange on the wrist.

```
   phone / Mac browser  ──http://astrolabe.local/pythia/──▶  watch (ESP32-S3)
   ├─ wllama.wasm + Qwen GGUF (cached after first load)      ├─ httpd :80, /sdcard/pythia
   ├─ castalia.institute APIs (quotes, ticker, faculty…)      ├─ mDNS "astrolabe" on Wi-Fi + USB
   └─ api.github.com (save transcript with your PAT)         └─ pythia face: QR, links, last reply
```

## USB handoff

Boot keeps the ROM **USB-Serial/JTAG** console, so flashing and the serial
console work as always. TinyUSB (CDC console + NCM network) is compiled in but
only installed when the Pythia face is selected:

1. entering the face detaches Serial/JTAG (drive SE0, let the host post a
   disconnect, gate the block — the 175C bench procedure), installs TinyUSB,
   moves the console to CDC and waits up to 10 s for the host to bind NCM;
2. if no host binds, everything is torn down and Serial/JTAG comes back
   (`USB no host (wifi only)` on the face);
3. leaving the face uninstalls TinyUSB and re-attaches Serial/JTAG.

State is visible on the face and in `faculty175_usb_pythia_state()`.

## Build and flash

```bash
export IDF_PATH=~/esp/esp-idf            # ESP-IDF 5.5
./scripts/astrolabe185b_build.sh build
./scripts/astrolabe185b_build.sh flash-core -p /dev/cu.usbmodemXXXX
```

The default `faculty` build carries the face. `astrolabe185b/sdkconfig.defaults`
now boots on Serial/JTAG with `TINYUSB_CDC` + `TINYUSB_NET_MODE_NCM` compiled
in, MSC off, FAT long filenames on (`CONFIG_FATFS_LFN_HEAP`) and larger TCP
windows; the SD-boot variants (`cyber`, `claw`, `recovery`) get their MSC
composite from `sdkconfig.cyber.defaults`. An existing `sdkconfig` must have
those keys updated (or be regenerated) — `idf.py reconfigure` will not flip
values already present.

## SD card payload

```bash
scripts/pythia_sd_prepare.sh /Volumes/<sdcard>
```

fetches `@wllama/wllama` from npm, the Qwen2.5-1.5B-Instruct Q4_K_M GGUF
(~1.1 GB) from Hugging Face, and copies `astrolabe185b/pythia_sd/index.html`:

```
pythia/index.html
pythia/vendor/wllama/index.js
pythia/vendor/wllama/wasm/wllama.wasm
pythia/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

`--model <url>` swaps the GGUF (anything llama.cpp loads, < 2 GB per file);
`--skip-model` prepares a page-only card. Downloads are cached in
`~/.cache/astrolabe-pythia` and resume.

No card reader? Leave the card in the watch, select the Pythia face and let
the firmware write it over the network (`PUT /pythia/<path>` streams to
`/sdcard/pythia`):

```bash
scripts/pythia_sd_prepare.sh --push http://astrolabe.local
```

Files go smallest-first so the page works while the GGUF is still streaming
(~1 MB/s over USB NCM, a few MB/s over Wi-Fi).

## Hosted copy

The same page is published at `https://astrolabe.castalia.institute/pythia/`
(GitHub Pages stages `astrolabe185b/pythia_sd/index.html` as
`docs/pythia/index.html`). Without a watch it loads wllama from jsDelivr and the
GGUF from Hugging Face; Castalia and GitHub features work the same. A watch-
served copy prefers the vendored runtime and the SD-card model.

## Reaching the watch

| Link | Address | Notes |
|------|---------|-------|
| USB NCM | `http://astrolabe.local/pythia/` or `http://172.31.77.1/pythia/` | Host gets `172.31.77.2` from the watch's DHCP. macOS 13+ and Linux bind CDC-NCM natively. |
| Wi-Fi | `http://astrolabe.local/pythia/` | Same hostname via the ESP mDNS component. |

The S3's USB peripheral is **Full-Speed (12 Mbit/s)**, so the first model load
runs at roughly 0.6–1 MB/s over NCM (~20 min for 1.1 GB) versus ~2–3 MB/s over
Wi-Fi. It is a one-time cost: wllama stores the weights in the browser's cache
storage and later loads are local.

## Firmware surface

| Route | Purpose |
|-------|---------|
| `GET /pythia/*` | Static files from `/sdcard/pythia`, `Range` support, `Content-Length` (browser progress), COOP/COEP headers. `.gguf`/`.wasm` are sent immutable-cacheable. |
| `PUT /pythia/*` | Writes the body to `/sdcard/pythia/<path>` (parents created, `.part` then rename). Used by `pythia_sd_prepare.sh --push`. |
| `GET /api/pythia/config` | Hostname, link IPs, GGUFs on the card, public Castalia client config (`MYNAH_SUPABASE_URL` + anon key from `include/secrets.local.h`, service URLs). |
| `POST /api/pythia/note` | `{prompt, reply, model, client}` puts the exchange on the face; `{state}` alone is a heartbeat. |

Both the esp `mdns` component (Wi-Fi) and a small responder inside
`astrolabe175c/main/faculty175_usb_ncm.c` (USB segment, raw lwIP netif) answer
`astrolabe.local`. `faces set pythia` selects the face from the serial console;
it is enabled by default.

## In the browser

- **Model** — pick a GGUF from the SD card (or the Hugging Face fallback), load
  once, chat with streaming tokens and tok/s. Plain `http://` origins are not
  cross-origin isolated, so wllama runs **single-threaded** (~16 tok/s for
  1.5B Q4 on an M-series Mac, a few tok/s on a phone). To unlock threads in
  Chrome, add `http://astrolabe.local` to
  `chrome://flags/#unsafely-treat-insecure-origin-as-secure`; the page already
  sends the COOP/COEP headers.
- **Castalia sign-in** — "Sign in with GitHub" / "Sign in with Google" run
  Supabase Auth's implicit flow against the Castalia project (no SDK: the page
  calls `/auth/v1/authorize`, `/auth/v1/user`, `/auth/v1/token` directly). The
  session is kept in `localStorage`, refreshed as needed, and used as the
  bearer for castalia.institute services (`/quote`, `/ticker`, `/almanac`,
  `/faculty <slug> <question>`), whose results become context for the local
  model. Signed out, the public anon key is used instead.
- **GitHub read/write** — signing in with GitHub requests the `repo` scope, so
  the Supabase session also carries a GitHub `provider_token`. With it the page
  lists your repositories, browses a folder, attaches any file as context,
  saves the transcript as `<folder>/<timestamp>.md` (`/save`) and appends the
  conversation to `<folder>/journal.md` (`/journal`, read-modify-write with the
  blob SHA). A fine-grained PAT (Contents: read/write) is the offline fallback.
  Tokens are sent only to `api.github.com`; nothing about them reaches the
  watch.

### Where the page gets its Supabase client config

| Copy | Source |
|------|--------|
| served by the watch | `GET /api/pythia/config` (`MYNAH_SUPABASE_URL` + anon key from `include/secrets.local.h`); when those are empty it loads `https://astrolabe.castalia.institute/pythia/config.js` |
| hosted on GitHub Pages | `docs/pythia/config.js`, written by `.github/workflows/deploy-github-pages.yml` from the `MYNAH_SUPABASE_ANON_KEY` repository secret |

### One-time Supabase / GitHub setup (dashboard, not code)

1. **GitHub OAuth App** (github.com → Settings → Developer settings): callback
   URL `https://pilmscrodlitdrygabvo.supabase.co/auth/v1/callback`.
2. **Supabase → Authentication → Providers → GitHub**: enable, paste the app's
   client ID/secret.
3. **Supabase → Authentication → URL configuration → Redirect URLs**: add
   `https://astrolabe.castalia.institute/pythia/`, `http://astrolabe.local/pythia/`
   and `http://172.31.77.1/pythia/` (the implicit flow returns the session in
   the URL fragment, so each origin the page is opened from must be listed).
4. Google sign-in works as soon as the Google provider that Mynah already uses
   is enabled for the same project.

## Limits and follow-ups

- The httpd task serves one request at a time; the face API is unresponsive
  while a GGUF is streaming (the page tolerates this).
- Plain HTTP only. A self-signed HTTPS listener would make the origin secure
  (threads + WebGPU) at the cost of TLS throughput on the S3.
- `ask-faculty-voice` uses Gemini server-side; the local model only sees its
  text reply.
