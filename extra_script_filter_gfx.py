"""Compile only GFX sources used by Astrolabe (QSPI + CO5300 + core drawing)."""
Import("env")


_KEEP = (
    "Arduino_ESP32QSPI.cpp",
    "Arduino_CO5300.cpp",
    "Arduino_DataBus.cpp",
    "Arduino_G.cpp",
    "Arduino_GFX.cpp",
    "Arduino_GFX_Library.cpp",
    "Arduino_TFT.cpp",
    "Arduino_TFT_18bit.cpp",
)


def _filter_gfx(node):
    path = node.get_path()
    if "GFX_Library_for_Arduino" not in path or not path.endswith(".cpp"):
        return node
    return node if any(k in path for k in _KEEP) else None


env.AddBuildMiddleware(_filter_gfx)
