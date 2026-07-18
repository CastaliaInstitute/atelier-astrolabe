# Raspberry Pi Wi-Fi KM and USB screen

This feature set belongs to the 1.75C `cyber` build variant:

```bash
ASTROLABE175C_VARIANT=cyber ./scripts/astrolabe175c_build.sh build
```

The default `faculty` build does not expose these faces, USB functions, HTTP
controls, or embedded installer files. Faculty and Cyber also use separate,
variant-checked OTA manifests.

Astrolabe normally exposes the ESP32-S3 fixed USB Serial/JTAG console. Selecting
the `USB Screen` face deliberately disconnects that profile and re-enumerates
with three USB functions:

- CDC serial console and framed screen upload
- internal `usbflash` mass storage
- composite HID keyboard and mouse

USB NCM networking is intentionally disabled because these functions consume the ESP32-S3's available USB endpoints. Keyboard and mouse commands can arrive over the physical CDC console or the paired Wi-Fi control page.

Leaving `USB Screen` removes CDC, MSC, and HID together and restores USB
Serial/JTAG. Host serial sessions and mounted MSC volumes therefore disconnect
during either profile transition. Eject the installer volume before leaving the
face if the host has written to it.

The MSC volume is an installer drive, not a Raspberry Pi boot drive. Astrolabe has only about 14.9 MiB of MSC capacity, and Raspberry Pi OS does not auto-run programs from removable storage.

## Install on an unmodified Raspberry Pi OS desktop

No Astrolabe software needs to be preinstalled. Wait for the Raspberry Pi desktop to finish logging in and for the installer USB drive (containing the `ASTROLABE` directory) to mount. Open Astrolabe's `HID Touchpad` face and tap twice within ten seconds. The first tap arms the operation; the face changes to `TAP AGAIN: INSTALL`. The second tap opens a terminal with `Ctrl+Alt+T` and runs the installer from the mounted MSC volume.

The installer uses `sudo apt-get`, so Raspberry Pi OS may ask for the user's password. It installs a desktop autostart entry and starts the screen agent immediately. The same guarded action is available over CDC by issuing `km install` twice within ten seconds.

## Keyboard and mouse

Open the `HID Touchpad` face to see the per-boot six-digit pairing code, then visit:

```text
http://ASTROLABE_IP/km
```

The browser releases all held keys and mouse buttons when it loses focus. Firmware also has a 1.5-second held-input watchdog.

The same operations are available over CDC:

```text
km status
km mouse 20 -5 0 0
km click 1
km key Enter tap
km key KeyC down 1
km key KeyC up 0
km type "hello from Astrolabe"
km release
```

Modifier bits are left Ctrl `1`, Shift `2`, Alt `4`, and GUI/Super `8`.

## USB screen

This is a software display, not USB DisplayPort alternate mode. A Linux userspace agent captures the desktop, scales it to 466×466, encodes a baseline JPEG, and sends it to Astrolabe's `USB Screen` face. The latest frame remains buffered while another face is selected. It becomes available after the Pi has booted far enough to run the agent; it cannot display the Pi firmware rainbow screen or early boot console.

On Raspberry Pi OS:

```bash
python3 -m venv ~/.venvs/astrolabe-screen
~/.venvs/astrolabe-screen/bin/pip install mss pillow pyserial requests
~/.venvs/astrolabe-screen/bin/python scripts/astrolabe_pi_screen.py \
  --usb /dev/ttyACM0 --fps 4
```

Wi-Fi frame transport is also available and requires the on-device pairing code:

```bash
~/.venvs/astrolabe-screen/bin/python scripts/astrolabe_pi_screen.py \
  --wifi http://ASTROLABE_IP --pair 123456 --fps 4
```

The CDC wire protocol is deliberately simple:

```text
host:   screen put jpeg BYTES\n
device: screen: READY bytes=BYTES\n
host:   BYTES raw JPEG octets
device: screen: OK err=ESP_OK bytes=BYTES\n
```

Frames must be baseline JPEG, no larger than 466×466 or 256 KiB. `screen stop` clears the buffered frame and returns the `USB Screen` face to its waiting state.
