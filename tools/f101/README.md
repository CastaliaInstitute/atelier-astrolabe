# Astrolabe from Codex on F101

This setup targets the **1.85B with its removable SD card**. Codex controls faces
over BLE. `linux` exposes the SD-backed USB disk, NCM network, and CDC console;
`wifi-setup` enables Wi-Fi configuration. Firmware updates use USB flashing.

The requested boot target is x86-64 first: a small Linux environment on the SD
card, followed by keyboard, mouse, and screen control through Astrolabe. The
existing card must be inspected before preparing or replacing its boot image.
The repository's Linux/HID status faces alone do not implement a working KVM.

## Codex tools

| Tool | Purpose |
|---|---|
| `astrolabe_ble_scan` | Discover Astrolabe through Android Bluetooth |
| `astrolabe_ble_pair` | Pair and name an additional Astrolabe without replacing existing peers |
| `astrolabe_ble_peers` | List the locally remembered Astrolabe peers |
| `astrolabe_ble_status` | Read face, Wi-Fi, firmware hash, and SD capacity |
| `astrolabe_ble_select_face` | Select an enabled face over authenticated BLE |
| `astrolabe_ble_select_face_all` | Select an enabled face on every paired Astrolabe |
| `astrolabe_ble_wifi` | Configure Wi-Fi or select saved credentials |
| `astrolabe_sd_status` | Probe the attached SD disk and partitions without writes |
| `astrolabe_inventory` | Find supported USB devices and build location |
| `astrolabe_console` | Send one bounded firmware console command |
| `astrolabe_monitor` | Read bounded USB logs |
| `astrolabe_build_cyber` | Build the 1.85B Cyber firmware |
| `astrolabe_flash_cyber` | Back up and USB-flash the verified image and layout |
| `astrolabe_job` | Poll a build/flash job |
| `astrolabe_target_screen` | Capture the booted Linux desktop through SSH |
| `astrolabe_target_click` | Click the Linux desktop |
| `astrolabe_target_type` | Type into its focused window |
| `astrolabe_target_keys` | Send keys and keyboard chords |

Cyber boots on `pocketwatch` with the hardware USB Serial/JTAG console available
before storage, BLE, and display initialization. TinyUSB does not start at boot.
Explicitly selecting `linux` hands the shared USB PHY to TinyUSB CDC, SD-backed
MSC, and NCM. These remain active across face changes until reset; eject the
host's SD volume before resetting or disconnecting. Hardware RESET restores the
boot configuration. BOOT held during RESET provides ROM download recovery.

Useful console reads include `qa board`, `qa status`, `faces profile`, and
`wifi status`. `bootloader` enters ROM download mode on the new firmware.
Responses can contain asynchronous logs; receiving bytes does not prove success.
Jobs are asynchronous: poll for a final result in `.state/<job-id>/`.

## Build and installation

The checkout is `CastaliaInstitute/atelier-astrolabe`, based on `integration`.
`tools/f101/build.sh` builds `astrolabe185b` with the Cyber variant and
`sdkconfig.defaults;sdkconfig.f101.defaults`. The F101 configuration enables
16MB flash, 8MB octal PSRAM, BLE, and CDC/SD-MSC/NCM. It skips voice pipeline
startup and uses size optimization, PSRAM allocations, and flash-resident
radio code to preserve internal RAM for stacks and DMA. LVGL uses malloc.
The I²C bus uses GPIO11/10, keeping the SDMMC pins free.
See [Waveshare's board reference](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85B).

Installed prerequisites:

- ESP-IDF v5.5.1 at `/root/esp/esp-idf`, ESP32-S3 toolchains, CMake and Ninja.
- IDF Python `/root/.espressif/python_env/idf5.5_py3.13_env`.
- Python with venv/pip, Java, libusb, and `blkid`.
- Android SDK `android-35/android.jar`, ECJ 3.26.0 (`ecj.jar`) and R8 8.3.37
  (`r8.jar`) in `/opt/astrolabe-android`. These prerequisites are not vendored.

Run `bash tools/f101/setup.sh` to install pinned Python packages, validate the
local control key, compile/install the Android helper, and register the
`astrolabe-f101` [Codex MCP server](https://developers.openai.com/codex/mcp).
Restart the MCP connection or start a new task to discover the tools.
Remove the registration with `codex mcp remove astrolabe-f101`.

The rooted F101 runs Kali in a chroot. The Java helper uses Android 13 Bluetooth
interfaces as the Android shell user. USB uses libusb around the device file,
since this kernel lacks `cdc_acm`. Hardware operations use bounded wake locks;
USB access and firmware jobs have exclusive locks.

BLE writes require encryption and the key in ignored `.state/control-key`,
compiled through ignored `include/secrets.local.h` (both mode 0600). Setup does
not overwrite existing firmware secrets. Wi-Fi credentials go through stdin,
not process arguments. Cyber's legacy settings characteristics are read-only.

F101 can retain more than one Android LE bond. Use `astrolabe_ble_pair` once for
each scanned address; the local registry is `.state/paired-astrolabes.json`.
Pairing a new Astrolabe never removes existing peers. `astrolabe_ble_select_face_all`
fans a face choice out to all registered peers and returns a per-device result.

## USB-only partition layout

The 16MB layout now has one **9MB application partition** at `0x20000` in place
of the old three 3MB application slots. NVS, boot-selection metadata, PHY data,
voice scratch (`0x920000`), assets (`0xa20000`), and internal USB FAT (`0xc00000`)
retain their offsets. OTA is not used.

Flashing checks the 1.85B identity, build receipt hashes, application capacity,
and unchanged data offsets. It backs up the installed application and partition
table, then writes the new partition table and application. It preserves the
bootloader and data volumes; it does not format or write the removable SD card.
This replaces the old factory application; recovery copies are local backups.
The earlier 1.75C build is incompatible and rejected by the tools.

The backup directory includes `application-backup.bin`, `partitions.bin`, and
a SHA-256 record. Keep it for recovery through ROM USB. Do not restore the old
application without its matching partition table.

## Verification

The attached board identifies as 1.85B, MAC `a0:f2:62:e4:3f:10`, ESP32-S3 rev 0.2,
16MB flash, and embedded 8MB PSRAM. Live MCP USB reads and invalid-input/USB-lock
checks pass. Its original firmware had nearly exhausted internal RAM and failed
BLE advertising. Switching its blank `faculty` face to `pocketwatch` loaded the
clock background and produced clock-update logs.

The first BLE-enabled build exceeded internal DRAM at link time. The revised
memory configuration builds successfully. The first image was USB-flashed and
verified on 2026-09-13, including the 9MB partition layout, after a complete
checksum-verified backup. However, BLE did not appear after reboot and USB was
hidden by the default face, so runtime validation failed. A recovery build now
starts the hardware USB Serial/JTAG console and BLE before peripheral
initialization, with TinyUSB deferred until explicit Linux-face activation.
Hardware validation awaits physical BOOT/RESET access. SD contents and physical
end-to-end KVM validation remain pending.

Checks:

```sh
tools/f101/.venv/bin/python tools/f101/test_smoke.py
tools/f101/.venv/bin/python tools/f101/test_usb_layout.py
tools/f101/.venv/bin/python tools/f101/test_targets.py
tools/f101/.venv/bin/python tools/f101/test_rom_backup.py
```

Use `test_smoke.py --no-usb` only when intentionally skipping hardware checks.

## Recovery on F101

Hold BOOT, press and release RESET, then release BOOT with USB connected. Call
`astrolabe_flash_cyber(recovery=True)` and poll the returned job. This uses the
previously recorded identity in ignored `.state/board.json`; a different ROM MAC
is rejected before writes. Backups stay in ROM between commands so firmware
cannot hide the USB port midway through recovery.

F101's stub transfers failed with corrupt or stalled reads, including with
esptool 4.12.0. The worker uses `--no-stub`, verifies 64KB backup chunks against
ROM-computed MD5, retries failed reads at most three times, then checks the whole
backup. Successful writes are also verified by esptool. The first successful
backup is `.state/36094b35c9a04130b10fb1e9352fa22b/application-backup.bin`, paired
with `partitions.bin` in that directory.

The local x86-64 image and its builder are described in [linux/README.md](linux/README.md).

All fifteen MCP tools are discoverable. The four target-desktop tools passed against
an isolated x86-64 QEMU guest, including an observed terminal command and its output
in the returned screenshot. This validates the Linux/SSH/RFB path, not the physical
Astrolabe USB bridge. `test_kvm.py` requires that dedicated local VM on port 22222.

### 2026-09-14 recovery validation

The JTAG-at-boot image exposed startup and system-event stack overflows.
Increasing those stacks to 16 KB and 8 KB respectively stopped the reboot loop.
The corrected image was flashed with device hash verification, and BLE status
returned the matching ELF hash (`97dd78f14`). Hardware USB Serial/JTAG logs remain
available without TinyUSB at boot. USB command replies have not been verified;
the touch controller reports repeated I2C failures. Physical display and SD/KVM
validation remain outstanding.

## Rear-display face synchronization

See [companion/README.md](companion/README.md) for the installed F101 app bridge,
startup command, current validation limits, and diagnostic status files.
