"""Compile only GFX sources used by Astrolabe (QSPI + display drivers + core drawing)."""
Import("env")

from pathlib import Path


_KEEP = (
    "Arduino_ESP32QSPI.cpp",
    "Arduino_CO5300.cpp",
    "Arduino_ST77916.cpp",
    "Arduino_DataBus.cpp",
    "Arduino_G.cpp",
    "Arduino_GFX.cpp",
    "Arduino_GFX_Library.cpp",
    "Arduino_TFT.cpp",
    "Arduino_TFT_18bit.cpp",
)


def _filter_gfx(node):
    path = node.get_path()
    if not ("GFX_Library_for_Arduino" in path or "GFX Library for Arduino" in path) or not path.endswith(".cpp"):
        return node
    return node if any(k in path for k in _KEEP) else None


env.AddBuildMiddleware(_filter_gfx)

arduino_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if arduino_dir:
    arduino_dir = Path(arduino_dir)
    env.Append(
        CPPPATH=[
            str(arduino_dir / "cores" / "esp32"),
            str(arduino_dir / "variants" / "esp32s3"),
            str(arduino_dir / "libraries" / "Wire" / "src"),
        ]
    )
