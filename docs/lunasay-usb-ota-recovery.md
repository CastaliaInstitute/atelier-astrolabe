# LunaSay recovery procedure

This procedure recovers a LunaSay prototype without relying on its physical
BOOT button. The production support version still needs device photographs and
a production-signed release URL.

## Normal no-button recovery

LunaSay exposes the ESP32-S3 USB Serial/JTAG interface by default. On macOS the
console normally appears as `/dev/cu.usbmodem*` at 115200 baud.

1. Connect USB and locate the console:

   ```bash
   ls /dev/cu.usbmodem*
   ```

2. Open a 115200-baud serial terminal and request the recovery image:

   ```text
   ota status
   ota boot factory
   ```

3. After the reboot, confirm `running=factory`. A valid product image can be
   selected without a physical button:

   ```text
   ota boot ota
   ```

4. The factory image verifies that an OTA slot contains a valid ESP image,
   selects it, and reboots. Confirm `running=ota_0` or `running=ota_1` with
   `ota status`.

`ota auto off` temporarily disables automatic manifest polling during bench
diagnosis. Re-enable it only after the production release manifest exists.

## Replace the factory image over USB Serial/JTAG

Use a LunaSay build from the `integration` release line:

```bash
source "$HOME/esp/esp-idf/export.sh"
ASTROLABE175C_VARIANT=lunasay ./scripts/astrolabe175c_build.sh build
cd astrolabe175c
idf.py -B build -p /dev/cu.usbmodemXXXX app-flash
```

`app-flash` writes the factory partition and preserves NVS, storage, OTA slots,
and OTA selection data. If the device restarts into an older OTA slot, issue
`ota boot factory` over the console to enter the newly written factory image.

For a fully erased or corrupted partition table, use the complete `idf.py
flash` path only after exporting any recoverable customer configuration. A
complete flash can replace partition metadata and must not be the first support
step.

## Local integrity and recovery validation

With the device and laptop on the same trusted LAN, run:

```bash
python3 scripts/lunasay_ota_recovery_validate.py \
  --port /dev/cu.usbmodemXXXX \
  --host LAPTOP_LAN_IP \
  --device-ip DEVICE_LAN_IP
```

The validator deliberately rejects a wrong SHA-256, installs the same image
with its correct SHA-256, exercises factory and product boot routing, leaves the
device on the product slot, and writes evidence under `artifacts/qa/`.

This local HTTP test proves image-integrity and recovery mechanics only.
Production release approval additionally requires an HTTPS-hosted LunaSay
manifest signed by the production key, correct-channel rejection, an
interrupted-transfer test, and preserved hashes/logs.
