# USB Audio Class gadget (Astrolabe)

Make the Waveshare ESP32-S3 watch enumerate as a **USB speaker** (and later **microphone**) on a host, bridging **UAC isochronous streams** to the onboard **ES8311 / ES7210** I2S path.

## Milestones

| Phase | Scope | Build env |
|-------|--------|-----------|
| **M1** | UAC **speaker** (host → watch), 48 kHz stereo | `waveshare_s3_175_uac` |
| **M2** | UAC **microphone** (watch → host) | same |
| **M3** | Audio arbiter vs voice/TTS; VBUS-gated mode; composite CDC+UAC | partial (`pm_audio_route`) |

## Firmware layout

- `pm_speaker_pcm` — shared I2S TX / ES8311 PCM path
- `pm_usb_uac` — `espressif/usb_device_uac` callbacks (M1: `output_cb` only); active when `CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0` in `sdkconfig`
- `pm_audio_route` — onboard vs USB speaker path (NVS); **swipe up** → USB, **swipe down** → onboard (skipped on Spotify / Synastry / Moon). Onboard mic + MP3/TTS when route is onboard; host UAC when route is USB.
- Default CI env `waveshare_s3_175` — unchanged (no UAC, USB CDC console)

## Build and flash (UAC)

**Requires [pioarduino](https://github.com/pioarduino/platform-espressif32) (ESP-IDF 5.x + Arduino 3.x).** Stock `espressif32@6.13` ships IDF 4.4 and cannot link `usb_device_uac`.

```bash
# One-time: vendor archives for offline / CI (optional if network available during hybrid)
./scripts/fetch_uac_components.sh

# Install UAC platform packages (first run)
pio pkg install -e waveshare_s3_175_uac

PIO_ENV=waveshare_s3_175_uac ./scripts/build.sh
pio run -e waveshare_s3_175_uac -t upload
```

Repo layout:

- `main` → symlink to `sketches/Astrolabe` (pioarduino hybrid IDF expects a `main` component)
- `custom_sdkconfig = file://sdkconfig.uac.defaults` — 48 kHz stereo speaker, macOS volume, no USB CDC on boot
- `custom_component_add = espressif/usb_device_uac @ 1.2.3` — hybrid IDF library build
- `sdkconfig.uac.defaults` — UAC Kconfig (also sets `CONFIG_ARDUINO_LOOP_STACK_SIZE=24576`)

The first `waveshare_s3_175_uac` build runs a **hybrid IDF compile** (~5–10 min) to rebuild Arduino libs with UAC enabled, then compiles the sketch. Subsequent builds are faster.

**Known issue:** pioarduino hybrid may fail with duplicate compile rules for `usb_descriptors.c` inside `usb_device_uac` (TinyUSB integration). If that happens, clean `managed_components/` and `.pio/build`, retry; track upstream pioarduino / esp-iot-solution.

## Host test (M1)

1. Flash `waveshare_s3_175_uac`, plug USB-C on the **OTG** port (GPIO19/20), not Serial/JTAG only.
2. On macOS: **Audio MIDI Setup** — select **Astrolabe UAC** as output.
3. Play audio; sound should route to the watch speaker via `pm_speaker_pcm`.

## USB / debug notes

- M1 uses **TinyUSB device (OTG PHY)** via `usb_device_uac`, not the built-in **USB Serial/JTAG** stack (`303A:1001`).
- The UAC env **disables** `ARDUINO_USB_CDC_ON_BOOT` — use **JTAG** or Wi‑Fi logging during bring-up.
- Confirm the 1.75C USB-C port is wired to **USB OTG** per the Waveshare schematic.

## Related code

- Voice path (default build): `pm_mic`, `pm_speaker`, `pm_voice`
- PMIC / charging: AXP2101 (future: auto-enable UAC when VBUS present)
