"""Install idf_component.yml for USB UAC PlatformIO env (waveshare_s3_175_uac)."""
Import("env")

from pathlib import Path
from shutil import copyfile

PROJECT = Path(env["PROJECT_DIR"])
SRC = PROJECT / "uac" / "idf_component.yml"
# Component manager reads manifest next to the sketch (PlatformIO src_dir).
DST = PROJECT / "sketches" / "Astrolabe" / "idf_component.yml"

if SRC.is_file():
    copyfile(SRC, DST)
    print("extra_script_uac_deps: installed idf_component.yml")
