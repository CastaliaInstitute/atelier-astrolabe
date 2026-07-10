"""Block deprecated PlatformIO hardware firmware targets by default."""
Import("env")

import os

pioenv = env.subst("$PIOENV")

if pioenv.endswith("_qemu"):
    Return()

if os.environ.get("ASTROLABE_ALLOW_DEPRECATED_PLATFORMIO") == "1":
    print(
        "warning: using deprecated PlatformIO firmware target "
        f"{pioenv} because ASTROLABE_ALLOW_DEPRECATED_PLATFORMIO=1"
    )
    Return()

print(
    "\n"
    f"error: PlatformIO target '{pioenv}' is deprecated for Astrolabe hardware.\n"
    "\n"
    "Use the ESP-IDF 1.75C firmware instead:\n"
    "  export IDF_PATH=/path/to/esp-idf\n"
    "  ./scripts/astrolabe175c_build.sh flash-core -p /dev/cu.usbmodemXXXX\n"
    "\n"
    "For the 1.75C board currently on USB, identify first with:\n"
    "  ./scripts/astrolabe175c_identify.sh /dev/cu.usbmodemXXXX\n"
    "\n"
    "If you intentionally need this legacy PlatformIO sketch, rerun with:\n"
    "  ASTROLABE_ALLOW_DEPRECATED_PLATFORMIO=1 pio run -e "
    f"{pioenv}\n"
)
env.Exit(1)
