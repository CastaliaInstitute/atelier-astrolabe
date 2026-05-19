#!/usr/bin/env python3
"""Remove duplicate usb_descriptors.c target_sources (pioarduino hybrid + usb_device_uac)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "# pioarduino: usb_descriptors compiled once on usb_device_uac"


def patch_file(path: Path) -> bool:
    if not path.is_file():
        return False
    text = path.read_text()
    if MARKER in text:
        return False
    new = re.sub(
        r'\n\s*target_sources\(\$\{tusb_lib\} PUBLIC "\$\{COMPONENT_DIR\}/tusb/usb_descriptors\.c"\)',
        "",
        text,
        count=1,
    )
    if new == text:
        return False
    if "tusb/usb_descriptors.c" not in new:
        new = new.replace(
            "idf_component_register(SRCS usb_device_uac.c",
            "idf_component_register(SRCS usb_device_uac.c tusb/usb_descriptors.c",
            1,
        )
    if MARKER not in new:
        new = new.replace(
            "if(NOT CONFIG_USB_DEVICE_UAC_AS_PART)",
            f"{MARKER}\nif(NOT CONFIG_USB_DEVICE_UAC_AS_PART)",
            1,
        )
    path.write_text(new)
    return True


def cmake_paths(root: Path) -> list[Path]:
    direct = root / "CMakeLists.txt"
    if direct.is_file() and root.name == "espressif__usb_device_uac" or (
        direct.is_file() and (root / "usb_device_uac.c").is_file()
    ):
        return [direct]
    nested = root / "managed_components/espressif__usb_device_uac/CMakeLists.txt"
    if nested.is_file():
        return [nested]
    return []


def main() -> int:
    roots = [Path(__file__).resolve().parents[1]]
    if len(sys.argv) > 1:
        roots = [Path(p) for p in sys.argv[1:]]
    n = 0
    for root in roots:
        for cmake in cmake_paths(root):
            if patch_file(cmake):
                print(f"patched {cmake}")
                n += 1
    return 0 if n else (1 if len(sys.argv) > 1 else 0)


if __name__ == "__main__":
    raise SystemExit(main())
