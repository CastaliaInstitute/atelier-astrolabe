# Recovery OTA And Modular Faces

**Status:** design target for the next factory-flash line. `integration` still
ships the native ESP-IDF OTA layout for Astrolabe variants. Do not switch
active partition tables until the recovery app, rollback path, and factory flash
workflow are validated end-to-end on hardware.

The goal is to make update reliability independent from the app being updated:
an OTA image must be unable to corrupt the component that decides what to boot
next or how to recover.

## Framework Direction

The recovery and long-lived platform framework should be ESP-IDF centric. Arduino
may remain as a compatibility component while existing faces are migrated, but
it should not define the OS architecture, recovery flow, heap policy, task
model, networking stack, or partition ABI.

Native IDF gives us the pieces this design depends on:

- `esp_ota_ops` and bootloader rollback semantics for pending/valid app states;
- `esp_https_ota` / `esp_http_client` with explicit TLS buffer tuning;
- `esp_partition` for strict write boundaries;
- `nvs_flash` namespaces for recovery-owned state;
- `esp_event`, FreeRTOS tasks, queues, and event groups for controlled lifetimes;
- `heap_caps` and heap tracing for internal RAM versus PSRAM budgeting;
- `esp_lcd`, I2S, USB, and HTTP server components that can be owned without
  Arduino globals or hidden allocation patterns.

The factory recovery app must be native ESP-IDF. Product apps should move toward
native IDF components as the default, with Arduino shims treated as temporary
leaf dependencies only where a display, touch, or face module has not yet been
ported.

## Boot Chain

1. **ROM + Espressif bootloader** stay minimal and immutable after factory
   flash. They validate image headers, secure-version policy, and OTA metadata.
2. **Factory recovery app** owns network update checks, manifest verification,
   app-slot selection, and rollback decisions. It lives in the factory app
   partition and is updated only by USB/factory flash.
3. **Product app slots** (`ota_0` / `ota_1`) contain Astrolabe, Faculty, Wand,
   or other day-to-day firmware. OTA writes only these slots.
4. **Face packs and assets** become their own A/B data partitions after the OS
   update path is boring and proven.

This is intentionally a recovery app, not a WiFi-capable custom bootloader. The
ESP32 bootloader should remain small, well-tested Espressif code. Recovery can
use normal ESP-IDF networking, TLS, logging, display, and serial diagnostics
without putting those risks inside the bootloader.

## Heap Architecture

Limited internal heap is a first-class architectural constraint. The updater
must be able to run after a product app has crashed, leaked, fragmented memory,
or failed partway through an update attempt. Recovery therefore keeps its runtime
small, short-lived, and mostly sequential.

Recovery heap rules:

- keep WiFi, TLS, manifest parsing, download, and flash write in one bounded
  update task instead of a feature-rich app shell;
- allocate large buffers from PSRAM when available, but keep all DMA, TLS
  handshake-critical, and flash-write buffers within explicit internal-RAM
  budgets;
- stream manifests and images; never require a full firmware image in RAM;
- parse manifests with fixed-size or arena-backed structures, not unbounded JSON
  object graphs;
- stop nonessential services before TLS, including display animations, BLE,
  audio, mDNS, and HTTP server endpoints not needed for recovery;
- expose a preflight heap gate using both free internal heap and largest
  contiguous internal block;
- fail closed with a visible/logged "low heap" status instead of starting an OTA
  that is likely to die during TLS or flash writes;
- reset the device between product-app runtime and recovery update execution
  when possible, so recovery starts from a clean heap.

Product app heap rules:

- treat PSRAM as the default home for framebuffers, decoded images, audio
  payloads, logs, and response bodies;
- reserve internal RAM for DMA, networking, TLS, task stacks, interrupt-facing
  buffers, and small control structures;
- make every network-capable subsystem expose a heap preflight check before it
  starts TLS;
- prefer one network/TLS operation at a time on low-memory devices;
- keep face modules loadable/unloadable with explicit teardown, especially for
  audio, BLE, image decoding, and HTTP clients;
- record low-heap deferrals in logs so field devices explain why they skipped
  an update or content fetch.

## Recovery Contract

The recovery app is the only firmware allowed to download and install product
app OTA images. Product apps may request an update by writing an NVS flag and
rebooting into recovery, but they do not perform their own final slot switch.

Recovery must:

- fetch the signed channel manifest for the device's `ota_channel`;
- reject images for the wrong hardware family, flash size, partition ABI, or
  secure-version policy;
- verify size, SHA-256, and signature before marking a slot bootable;
- write only inactive `ota_*` app slots for OS updates;
- mark the new app pending verification and rely on ESP-IDF rollback if it fails
  to confirm healthy boot;
- use native ESP-IDF OTA APIs instead of Arduino `Update`;
- run OTA as a bounded state machine with explicit heap preflight thresholds;
- expose USB serial and on-screen status for update failures;
- never accept OTA writes for bootloader, partition table, factory recovery, or
  eFuse configuration.

Product apps must:

- report `app_valid` only after display, input, WiFi, and the main loop survive
  the smoke window;
- preserve the recovery NVS namespace;
- never erase OTA metadata or write bootloader/partition/factory regions;
- provide a "reboot to recovery/update" command from settings and serial QA;
- avoid owning the final update decision; they request recovery, then reboot.

## Face Pack OTA

Face OTA should wait until the OS/recovery path is stable. The intended shape is
separate A/B data partitions, for example `faces_a` and `faces_b`, with a small
active-bank pointer in recovery-owned NVS.

Face-pack updates should:

- carry a manifest with pack id, ABI version, firmware compatibility, size,
  SHA-256, and signature;
- download into the inactive face bank;
- validate every referenced asset before switching the active bank;
- allow product apps to fall back to built-in faces if a pack is missing or
  incompatible;
- avoid changing the OS app slot unless the face ABI itself changes;
- use the same streaming, heap-gated downloader as firmware OTA.

This keeps "new faces/assets" from becoming "new firmware" once the core OS is
nailed down.

## Partition ABI

Partition layout is an ABI. Once devices have this recovery layout, changing it
requires a factory/USB flash or an explicitly tested migration image. Regular
OTA must not resize partitions.

Draft layouts live under [`deferred/`](deferred/):

- [`partitions_recovery_16mb.csv`](deferred/partitions_recovery_16mb.csv) for
  16 MB devices.
- [`partitions_32mb.csv`](deferred/partitions_32mb.csv) for 32 MB devices with
  larger face/data banks.

Before enabling either layout:

1. Build and factory flash recovery plus one product app.
2. Verify recovery can install `ota_0`, boot it, and receive app-valid.
3. Verify recovery can install `ota_1`, boot it, and receive app-valid.
4. Flash a deliberately crashing app and confirm rollback without USB recovery.
5. Flash a wrong-hardware manifest and confirm recovery refuses it.
6. Power-cycle during download and during slot switch; confirm recovery remains
   reachable.
