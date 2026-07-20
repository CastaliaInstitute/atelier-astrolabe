# Android PWA flash bridge

The Astrolabe Flasher can install as an Android PWA and flash an ESP32-S3 over
USB-C/OTG. A local CLI bridge lets that PWA flash the build in
`astrolabe175c/build` without publishing the binaries.

The bridge binds only to the development computer's loopback interface. Android
reaches it through `adb reverse`, so Chrome sees a trustworthy `localhost`
origin and the unpublished firmware is not served to the LAN.

## One-time Android setup

1. Install current Chrome and Android Platform Tools (`adb`) on the development
   computer.
2. On Android, enable **Developer options → Wireless debugging**.
3. Pair and connect from the computer using the addresses shown by Android:

   ```sh
   adb pair PHONE_IP:PAIRING_PORT
   adb connect PHONE_IP:DEBUG_PORT
   adb devices
   ```

Wireless ADB keeps the phone's USB-C port available for Astrolabe. The phone
and development computer must remain on the same network while the bridge is
starting.

## Build, connect, and flash

From the repository root:

```sh
source ~/esp/esp-idf/export.sh
./scripts/android_flash_bridge.py --build
```

For the Cyber variant:

```sh
./scripts/android_flash_bridge.py --build --variant cyber
```

## Build any Git branch or commit

Pass any locally resolvable branch, remote-tracking branch, tag, or commit. The
bridge creates a detached temporary worktree, builds there, and removes it when
the bridge stops. It never switches or cleans the current checkout.

```sh
# Local branch
./scripts/android_flash_bridge.py --ref feature/my-face

# Remote branch (fetch first)
git fetch origin feature/my-face
./scripts/android_flash_bridge.py --ref origin/feature/my-face

# Exact commit and Cyber variant
./scripts/android_flash_bridge.py --ref a1b2c3d --variant cyber
```

`--ref` implies `--build`. By default the temporary worktree receives a copy of
the local, gitignored `include/secrets.local.h`; use `--no-local-secrets` for a
credential-free build. Older or experimental refs with a stale LVGL audit can
be built explicitly with `--skip-lvgl-audit`.

The PWA labels the build with the requested ref and resolved commit. Flash
offsets come from that build's `flasher_args.json`, and the app size limit comes
from its generated partition table.

The command builds the firmware, validates all four images, creates an ADB
reverse tunnel, and opens the local flasher on Android. Then:

1. Optionally tap **Install Android app** or use Chrome's **Install app** menu.
2. Connect Astrolabe to the phone with a data-capable USB-C/OTG cable.
3. Tap **Connect WebUSB** and grant access to the Espressif device.
4. Keep **Include bootloader, partition table, and OTA metadata** enabled.
5. Tap **Flash selected image** and leave the bridge running until reset.

If automatic reset cannot enter the ROM loader, hold Astrolabe's BOOT control
while connecting it, then choose **Connect WebUSB** again.

## Reuse an existing build

Omit `--build` to serve the current build immediately:

```sh
./scripts/android_flash_bridge.py
```

An already-built checkout or exported ESP-IDF build directory can be served
directly and given a useful label:

```sh
./scripts/android_flash_bridge.py \
  --build-dir /path/to/other-worktree/astrolabe175c/build \
  --name "PR 248 device test"
```

Useful diagnostics:

```sh
./scripts/android_flash_bridge.py --check
./scripts/android_flash_bridge.py --help
```

The installed PWA expects the bridge on port `8097`. Keep that default for
ordinary use. A different port can be selected with `--port` when opening the
page through the CLI each time.

## Security and scope

- The bridge listens on `127.0.0.1`, not the LAN.
- Firmware and manifest responses use `Cache-Control: no-store`.
- The service worker does not cache `/bridge/` or `/releases/` payloads.
- USB access always requires an explicit Android permission prompt.
- This is a development/recovery path. Promoting firmware to `main` still uses
  the repository's device gate and `promote-integration.sh --flash-ok` flow.
