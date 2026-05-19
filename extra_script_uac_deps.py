"""UAC env: add USB identity defines for the Arduino TinyUSB audio path."""
Import("env")

from pathlib import Path

env.Prepend(CPPPATH=[str(Path("sketches/Astrolabe"))])

tinyusb_roots = []

try:
    libs_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")
except KeyError:
    libs_dir = None
if libs_dir:
    tinyusb_roots.append(Path(libs_dir) / "esp32s3/include/arduino_tinyusb")

arduino_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if arduino_dir:
    tinyusb_roots.append(Path(arduino_dir) / "tools/sdk/esp32s3/include/arduino_tinyusb")

for tinyusb in tinyusb_roots:
    if (tinyusb / "include").exists() and (tinyusb / "tinyusb/src").exists():
        env.Append(
            CPPPATH=[
                str(tinyusb / "include"),
                str(tinyusb / "tinyusb/src"),
            ]
        )
        break

env.Append(
    CPPDEFINES=[
        ("CONFIG_UAC_TUSB_MANUFACTURER", '\\"Castalia Institute\\"'),
        ("CONFIG_UAC_TUSB_PRODUCT", '\\"Astrolabe\\"'),
        ("CONFIG_UAC_TUSB_SERIAL_NUM", '\\"astrolabe\\"'),
    ]
)
