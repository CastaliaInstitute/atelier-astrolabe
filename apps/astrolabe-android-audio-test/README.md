# Astrolabe Android USB-C audio test

This native Android app turns a phone or tablet into the USB **host and audio
source** for Astrolabe. It detects Android USB audio outputs and sends generated
48 kHz, 16-bit stereo PCM directly to the selected USB route.

Tests included:

- 1 kHz left channel
- 1 kHz right channel
- 1 kHz stereo
- 100 Hz–10 kHz stereo sweep
- live Astrolabe USB capture → Bluetooth Classic A2DP bridge

## Bluetooth Classic bridge

Pair and connect a Bluetooth Classic A2DP speaker or headphones to Android,
then connect Astrolabe by USB-C and select **Bridge Astrolabe → Bluetooth**.
The app pins an `AudioRecord` to Astrolabe's UAC capture endpoint, duplicates
its 48 kHz mono PCM into stereo, and pins an `AudioTrack` to the connected A2DP
output.

This requires Astrolabe firmware to expose a UAC microphone/capture endpoint in
addition to (or instead of) its speaker endpoint. Android will request microphone
permission because USB capture uses the platform recording API. Bluetooth A2DP
adds buffering and codec latency, so this is suitable for functional listening
tests but not low-latency monitoring or precise round-trip latency measurements.

## Prerequisite: Astrolabe firmware

Astrolabe must enumerate on its native ESP32-S3 USB OTG port as a class-compliant
USB Audio Class output. The current `astrolabe175c` default USB profile is
Serial/JTAG or TinyUSB CDC+MSC; the Android app will correctly show **NOT
DETECTED** until a UAC firmware profile is running.

The repository's `uac/overlay_usb_device_uac` component contains the prior
ESP32-S3 UAC device implementation, but it is not currently connected to the
native `astrolabe175c` firmware or its `faculty175_audio_write_pcm()` path.

## Phone setup

1. Enable Developer options and Wireless debugging. USB debugging cannot occupy
   the same USB-C port while the phone is acting as the USB host.
2. Ensure Developer options → **Disable USB audio routing** is off.
3. Connect Astrolabe with a USB-C data/OTG cable. Use a powered USB-C hub if the
   phone cannot power both devices reliably.
4. Open **Astrolabe Audio Test**. A compatible firmware appears as `USB audio:
   READY`; select a channel test.

The app deliberately does not request raw USB permission or claim the audio
interface. Android's USB audio driver owns the interface and the app uses the
normal `AudioTrack` route APIs.

## Build and install

From the repository root:

```sh
./scripts/android_audio_test.sh build
./scripts/android_audio_test.sh install
./scripts/android_audio_test.sh start
```

To select one of multiple wireless ADB devices:

```sh
./scripts/android_audio_test.sh --device 192.168.1.50:37123 install
```
