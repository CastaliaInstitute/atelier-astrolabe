# Astrolabe Android flasher host

This APK lets a Galaxy Note Edge act as the USB host for Astrolabe. Firmware is
compiled locally on the development Mac, embedded into the APK, and then
flashed by Android Chrome through the app's persistent native USB bridge. The
firmware does not need to be published or downloaded from the Internet.

## Build and install

From the repository root:

```sh
./scripts/build_android_flasher.sh --install
```

The command finds ESP-IDF, builds the Faculty firmware, validates the complete
flash layout, builds the APK, detects the attached `SM-N915` ADB target, installs
the APK, and opens it. Use `--variant cyber` for a Cyber build. During rapid app
development, `--reuse-build` reuses the existing, already-validated firmware.
The normal command keeps the firmware LVGL audit enabled. For an experimental
branch whose known audit failure is being diagnosed separately, bypass it only
when intentional with `--skip-lvgl-audit`.

An exact ADB target can be supplied when needed:

```sh
./scripts/build_android_flasher.sh --reuse-build --install \
  --adb-serial 192.168.86.67:5555
```

## Flash on the Edge

1. Leave wireless ADB connected; the Edge USB port belongs to Astrolabe.
2. Connect Astrolabe with a data-capable OTG adapter/cable.
3. Open **Astrolabe Flasher** and grant USB access. Select the Android option to
   use this app by default for this Astrolabe device so bootloader re-enumeration
   does not require another prompt.
4. In Chrome, connect and flash the bundled build with the full flash layout
   enabled.
5. Keep the Edge awake and powered until the flash reaches 100% and Astrolabe
   restarts.

The foreground service keeps `http://localhost:8765/flasher/` and the native USB
bridge alive while Chrome is in front. It force-claims the CDC interfaces from
Android's `cdc_acm` driver and waits for the ESP32-S3 to re-enumerate during the
USB-JTAG bootloader reset sequence.

The APK is written to
`app/build/outputs/apk/debug/app-debug.apk`. Building this development transport
does not promote firmware to `main`; normal integration/device gates still
apply.
