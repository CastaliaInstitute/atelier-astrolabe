# Modular OTA (deferred)

**Status:** Backed out on `integration` (2026-05). Integration uses the same **16 MB / default partitions** as `main` (`app` @ `0x10000`). Factory flash: `./scripts/flash_factory.sh`.

Modular OTA (#60) remains on `feature/60-modular-ota` with the draft table in [`deferred/partitions_32mb.csv`](deferred/partitions_32mb.csv). Do not enable until `otadata` @ `0xE000` and upload @ `0x20000` are validated end-to-end.

## Draft partition layout (not active)

See [`deferred/partitions_32mb.csv`](deferred/partitions_32mb.csv) for the planned 32 MB layout (`ota_0`/`ota_1`, face packs, assets).
