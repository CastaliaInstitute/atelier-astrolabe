"""Rename PIO-installed GFX folder so object paths have no spaces (GCC -MMD / as break on spaces)."""
Import("env")

from pathlib import Path
import shutil


def rename_gfx_no_spaces():
    libdeps = Path(env["PROJECT_DIR"]) / ".pio" / "libdeps" / env["PIOENV"]
    bad = libdeps / "GFX Library for Arduino"
    good = libdeps / "GFX_Library_for_Arduino"
    if bad.is_dir() and not good.exists():
        shutil.move(str(bad), str(good))
    if good.is_dir():
        env.Append(CPPPATH=[str(good)])


rename_gfx_no_spaces()
