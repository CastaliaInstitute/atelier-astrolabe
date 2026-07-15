# Astrolabe Elecrow128 - CrowPanel 1.28 Rotary ESP32-S3

Native ESP-IDF bring-up target for the Elecrow CrowPanel 1.28-inch rotary
display. This is the small 240x240 GC9A01 round IPS unit with CST816D touch,
rotary encoder, knob press, and 5 onboard RGB LEDs.

Vendor reference cloned locally for bring-up:

```text
.local/vendor/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen
```

Upstream source:

```text
https://github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen
```

## Hardware

- ESP32-S3R8, 240 MHz
- 16 MB flash
- 8 MB octal PSRAM
- 1.28-inch 240x240 IPS round display
- GC9A01 SPI display controller
- CST816D capacitive touch controller at I2C address `0x15`
- Rotary encoder plus active-low press switch
- 5 RGB LEDs on one-wire data pin

## Vendor Pin Map

| Function | GPIO |
| --- | ---: |
| Display SCLK | 10 |
| Display MOSI | 11 |
| Display MISO | -1 |
| Display DC | 3 |
| Display CS | 9 |
| Display RST | 14 |
| Display backlight PWM | 46 |
| Touch SDA | 6 |
| Touch SCL | 7 |
| Touch INT | 5 |
| Touch RST | 13 |
| External I2C SDA | 38 |
| External I2C SCL | 39 |
| Encoder A / CLK | 45 |
| Encoder B / DT | 42 |
| Encoder switch | 41 |
| RGB LED data | 48 |
| Power indicator | 40 |
| Power enable 1 | 1 |
| Power enable 2 | 2 |

## OTA Layout

This target starts with an OTA-capable 16 MB partition table:

- factory app at `0x20000`
- `ota_0` and `ota_1`, 3 MB each
- SPIFFS storage for future assets and device state

The first firmware is a psychometer bring-up app. It initializes USB serial
logging, NVS, OTA rollback confirmation, power/backlight GPIOs, CST816D probe,
rotary input, Wi-Fi STA mode, and ESP-NOW broadcast.

Runtime behavior:

- rotating the knob moves between affect-face stops around the circumplex;
- arousal and valence are derived from the selected stop on the circumference;
- the knob press returns to the positive-valence stop;
- readings are broadcast every 250 ms and immediately after changes;
- serial logs print the current reading every 2 seconds.
- USB serial commands:
  - `status` prints the current reading and device MAC;
  - `shot` streams the current framebuffer as a 240x240 PPM screenshot.

ESP-NOW packet:

| Field | Type | Notes |
| --- | --- | --- |
| `magic` | `uint32_t` | `0x50535943` (`PSYC`) |
| `version` | `uint8_t` | currently `1` |
| `channel` | `uint8_t` | currently channel `6` |
| `size` | `uint16_t` | packet byte size |
| `seq` | `uint32_t` | incrementing packet sequence |
| `uptime_ms` | `uint32_t` | sender uptime |
| `mac` | `uint8_t[6]` | sender default MAC |
| `arousal` | `int8_t` | `0..100` |
| `valence` | `int8_t` | `0..100` |
| `focus_axis` | `uint8_t` | `0=arousal`, `1=valence` |
| `encoder_steps` | `int16_t` | signed detent count |
| `button_presses` | `uint16_t` | press count |

Display rendering and the full Astrolabe face runtime are next.

## Build And Flash

Requires ESP-IDF 5.x.

```bash
source "$IDF_PATH/export.sh"
./scripts/elecrow128_build.sh build
./scripts/elecrow128_build.sh -p /dev/ttyACM1 flash monitor
```

Capture a screenshot over USB serial:

```bash
sg dialout -c './scripts/elecrow128_screenshot.py -p /dev/ttyACM1 -o artifacts/elecrow128/psychometer-screenshot.png'
```

Current Linux serial permissions may require:

```bash
sudo chmod a+rw /dev/ttyACM1
```
