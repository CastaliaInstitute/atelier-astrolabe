# USB Audio Class gadget (Astrolabe)

Make the Waveshare ESP32-S3 watch enumerate as a **USB speaker** (and later **microphone**) on a host, bridging **UAC isochronous streams** to the onboard **ES8311 / ES7210** I2S path.

## Milestones

| Phase | Scope | Build env |
|-------|--------|-----------|
| **M1** | UAC **speaker** (host → watch), 48 kHz stereo | `waveshare_s3_175_uac` |
| **M2** | UAC **microphone** (watch → host) | same |
| **M3** | Audio arbiter vs voice/TTS; VBUS-gated mode; composite CDC+UAC | TBD |

## Firmware layout

- `pm_speaker_pcm` — shared I2S TX / ES8311 PCM path
- `pm_usb_uac` — `espressif/usb_device_uac` callbacks (M1: `output_cb` only)
- Default CI env `waveshare_s3_175` — unchanged (no UAC, USB CDC console)

## Build and flash (UAC)

```bash
PIO_ENV=waveshare_s3_175_uac ./scripts/build.sh
pio run -e waveshare_s3_175_uac -t upload
```

`extra_script_uac_deps.py` copies `uac/idf_component.yml` into `sketches/Astrolabe/` (pins `espressif/usb_device_uac` 1.2.3). `sdkconfig.uac.defaults` sets 48 kHz, 2ch speaker, no mic.

### Linking `usb_device_uac` (IDF 5+)

The Espressif UAC component requires **ESP-IDF ≥ 5.0**. PlatformIO env `waveshare_s3_175_uac` sets `ASTROLABE_USB_UAC=1` and compiles `pm_usb_uac.cpp`; if `usb_device_uac.h` is not on the include path, init logs a warning and returns false (PCM bridge still builds).

To link the component on a machine with IDF 5 / component manager support:

1. Ensure `sketches/Astrolabe/idf_component.yml` is present (run a UAC env build once, or copy from `uac/idf_component.yml`).
2. Use a PlatformIO + Arduino core toolchain that resolves managed components (ESP-IDF 5.1+ bundled with arduino-esp32 3.x), or build from an ESP-IDF project that depends on `espressif/usb_device_uac`.
3. Apply `sdkconfig.uac.defaults` via `board_build.sdkconfig_defaults`.

CI **default** env `waveshare_s3_175` does not enable UAC and is unchanged.

## Host test (M1)

1. Flash `waveshare_s3_175_uac`, plug USB-C.
2. On macOS: **Audio MIDI Setup** — select **Astrolabe UAC** as output.
3. Play music; audio should route to the watch speaker.

Enable `CONFIG_UAC_SUPPORT_MACOS=y` in `sdkconfig.uac.defaults` for macOS volume format.

## USB / debug notes

- M1 uses **TinyUSB device (OTG PHY)** via `usb_device_uac`, not the built-in **USB Serial/JTAG** stack (`303A:1001`).
- The UAC env **disables** `ARDUINO_USB_CDC_ON_BOOT` — `Serial` over USB may be unavailable. Use **OpenOCD/JTAG** (`pio debug`) or Wi‑Fi logging during bring-up.
- **Hardware:** Confirm the 1.75C USB-C port is wired to **USB OTG** (GPIO19/20), not only Serial/JTAG. If only Serial/JTAG is connected, UAC will not enumerate on the Type-C port; see Waveshare schematic.

## Related code

- Voice path (unchanged on default build): `pm_mic`, `pm_speaker`, `pm_voice`
- PMIC / charging: AXP2101 (future: auto-enable UAC when VBUS present)
