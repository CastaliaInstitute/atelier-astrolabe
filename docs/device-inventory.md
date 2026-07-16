# Device Inventory

Bench devices observed on 2026-05-26.

| Device | Port | Chip | MAC | USB identity | Notes |
|---|---|---|---|---|---|
| Babel Fish 1.75 S3 | `/dev/cu.usbmodem1101` | ESP32-S3, revision v0.2 | `a4:cb:8f:d6:41:94` | Espressif USB JTAG/serial, serial `A4:CB:8F:D6:41:94` | New Babel Fish translator unit; use `waveshare_s3_175_babel_fish`; authorized for the signed ESP-IDF integration OTA channel. |
| Cameo 1.75 S3 | `/dev/cu.usbmodem133401` | ESP32-S3, revision v0.2 | `a4:cb:8f:d6:42:60` | Espressif USB JTAG/serial, serial `A4:CB:8F:D6:42:60` | Pendant with Faculty bust home face; use `waveshare_s3_175_cameo`. |
| PocketWatch S3 | `/dev/cu.usbmodem1201` | ESP32-S3, revision v0.2 | `a0:f2:62:e3:06:44` | Espressif USB JTAG/serial, serial `A0:F2:62:E3:06:44` | Native USB serial/JTAG. |
| Smart Speaker 1.85 S3 | `/dev/cu.usbmodem134401` | ESP32-S3, revision v0.2 | `30:ed:a0:29:5a:b8` | Espressif USB JTAG/serial, serial `30:ED:A0:29:5A:B8` | 1.85 Spotify gadget bench unit; USB port suffix may change after hub/device re-enumeration. |
| Ocarina 1.75 S3 | last seen `/dev/cu.usbmodemA0F262E307841` | ESP32-S3 | `a0:f2:62:e3:07:84` | Espressif app USB serial, serial `A0F262E30784` | 1.75 Ocarina unit; not visible in the latest enumeration. |
| Enso 1.28 C3 | `/dev/cu.usbmodem1401` | ESP32-C3, revision v0.4 | `48:31:b7:3f:53:bc` | Espressif USB JTAG/serial, serial `48:31:B7:3F:53:BC` | 1.28 EEG/HRV headband bench unit; inventory only for now. |
| Seeed Sense Watcher | `/dev/cu.usbmodem56D50202623` | ESP32-S3, revision v0.2 | `d8:3b:da:75:c6:e4` | WCH USB Dual Serial, serial `56D5020262` | Second channel `/dev/cu.usbmodem56D50202621` did not answer `read-mac`. |
| LunaSay P4 | `/dev/cu.usbmodem5A360268091` | ESP32-P4, revision v1.0 | `30:ed:a0:e1:61:96` | USB Single Serial, serial `5A36026809` | Probe with `--no-stub`; stub upload reported a checksum error. |

Enumeration command:

```bash
./scripts/enumerate_esp_devices.sh
```

Resolve a current serial port by MAC before flashing:

```bash
./scripts/resolve_esp_port_by_mac.sh a4:cb:8f:d6:42:60
ASTROLABE_DEVICE_MAC=a4:cb:8f:d6:42:60 PIO_ENV=waveshare_s3_175_cameo ./scripts/ci-flash.sh
```

Manual probe command:

```bash
pio pkg exec --package tool-esptoolpy -- esptool.py --port <port> --baud 115200 --connect-attempts 3 --no-stub read-mac
```

## Private assignment database

Person-to-device assignments belong in a local registry, not in the public bench
inventory, because wearable BLE addresses and family ownership are personal
data. Keep the private file at:

```text
.local/device-assignments.json
```

The committed template is:

```text
docs/templates/device-assignments.example.json
```

The registry tracks:

- `people`: local person IDs, display names, and Castalia individual IDs.
- `familyRepositories`: private per-user GitHub repositories used for family
  synastry and wellness exchange after a user authorizes Castalia once.
- `devices`: Astrolabes, smart rings, short display IDs, BLE addresses,
  firmware identities, and useful GATT service details.
- `links`: ownership or pairing relationships, such as one person's Astrolabe
  and ring, plus explicit family wellness subscriptions between Astrolabes.

Family wellness subscriptions should share summaries by default, not raw BLE
packets: stress, HRV, sleep, and ring battery are enough for the family
synastry face and LLM/TTS prompts. Keep pairwise encryption material only in
the private `.local/device-assignments.json` file or the user's private family
repository; never commit real family keys or wearable addresses to public docs.

When assigning an Astrolabe over serial, set the same owner in firmware NVS:

```text
name Camille
castalia individual Camille
```

Then update `.local/device-assignments.json` with the Astrolabe MAC, current
serial port or mDNS name, and mark the assignment status as connected.

Short IDs are the normal UI and family-synastry identifier. Keep full MAC
addresses in the private local registry or protected Supabase provisioning
table; use `identifiers.shortId` for radar labels, prompts, and family
repository records. The Supabase `astrolabe-device-lookup` Edge Function
resolves a short ID to sanitized metadata without returning the MAC.
