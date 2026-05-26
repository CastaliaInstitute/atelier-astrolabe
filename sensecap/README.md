# Astrolabe on SenseCAP Watcher

This is a first ESP-IDF target for running Astrolabe directly on the SenseCAP
Watcher. It reuses Seeed's Watcher BSP from a local
`SenseCAP-Watcher-Firmware` checkout and boots into an Astrolabe camera face.
The Watcher uses the Astrolabe reflection baseline: SSCMA detections and face
metrics can be sent to Castalia LLM/TTS as uncertain mindfulness cues, not as
claims about identity, diagnosis, intent, truthfulness, personality, or stable
mental state.

By default, the build expects this checkout layout:

```text
CastaliaInstitute/
  astrolabe/
  sensecap/SenseCAP-Watcher-Firmware/
```

Use `-DASTROLABE_SENSECAP_FW_PATH=/absolute/path/to/SenseCAP-Watcher-Firmware`
if your Watcher firmware checkout lives somewhere else.

## Build

```sh
cd /Users/danielmcshan/GitHub/CastaliaInstitute/astrolabe/sensecap
source "$HOME/esp/esp-idf-v5.2.1/export.sh"
idf.py set-target esp32s3
idf.py -D CMAKE_POLICY_VERSION_MINIMUM=3.5 build
```

The CMake policy flag is needed with the current host CMake and older Watcher
components.

## Flash Safely

Verify that the attached device is the Watcher before flashing. The expected
MAC for this unit is `d8:3b:da:75:c6:e4`:

```sh
./check-watcher-mac.sh /dev/cu.wchusbserial56D50202623
```

Back up the factory NVS partition before first experiments:

```sh
mkdir -p artifacts/sensecap
esptool.py --port /dev/cu.wchusbserial56D50202623 --baud 2000000 \
  --chip esp32s3 --before default_reset --after hard_reset --no-stub \
  read_flash 0x9000 204800 artifacts/sensecap/nvsfactory-$(date +%Y%m%d-%H%M%S).bin
```

Then flash only the app slot:

```sh
idf.py --port /dev/cu.wchusbserial56D50202623 -b 2000000 app-flash
```

Avoid `erase-flash` and avoid flashing the partition table until we are
intentionally changing the Watcher's factory layout.
