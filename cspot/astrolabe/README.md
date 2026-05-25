# Astrolabe cspot Target

This is a small ESP-IDF firmware target for proving native Spotify Connect on
the Waveshare ESP32-S3 1.85 smart speaker hardware.

It uses [`cspot`](../../third_party/cspot) for Spotify Connect and a local
Astrolabe audio sink for the 1.85 I2S pins:

- V1: PCM5101-style I2S DAC on BCLK 48, LRCK 38, DOUT 47
- V2: ES8311 on I2C SDA 11/SCL 10, PA 15, MCLK 2, BCLK 48, LRCK 38, DOUT 47

Build and flash V2:

```sh
pio -c cspot/astrolabe/platformio.ini run -e astrolabe-cspot-s3-185-v2
pio -c cspot/astrolabe/platformio.ini run -e astrolabe-cspot-s3-185-v2 -t upload --upload-port /dev/cu.usbmodem1101
```

Build and flash V1:

```sh
pio -c cspot/astrolabe/platformio.ini run -e astrolabe-cspot-s3-185-v1
pio -c cspot/astrolabe/platformio.ini run -e astrolabe-cspot-s3-185-v1 -t upload --upload-port /dev/cu.usbmodem1101
```

Configure WiFi with ESP-IDF menuconfig under `Example Connection
Configuration`, or provide the corresponding sdkconfig defaults before build.
Once booted, the device advertises as `Astrolabe 1.85` in Spotify Connect.
