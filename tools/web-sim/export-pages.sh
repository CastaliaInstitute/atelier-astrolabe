#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${ASTROLABE_WEB_SIM_BUILD_DIR:-${ROOT}/build/web-sim}"
OUT_DIR="${ASTROLABE_WEB_SIM_PAGES_DIR:-${ROOT}/docs/sim}"

"${ROOT}/tools/web-sim/build.sh"

html_face_count="$(
  python3 - "$BUILD_DIR/astrolabe-web-sim.html" <<'PY'
import re
import sys
from pathlib import Path

html = Path(sys.argv[1]).read_text()
print(len(re.findall(r"<option\s+value=", html)))
PY
)"
enum_face_count="$(
  python3 - "$ROOT/src/faces/pm_faces.h" <<'PY'
import re
import sys
from pathlib import Path

header = Path(sys.argv[1]).read_text()
header = re.sub(r"/\*.*?\*/", "", header, flags=re.S)
body = re.search(r"enum class ClockFace\s*:[^{]+{(?P<body>.*?)\bkNumFaces\b", header, re.S)
if not body:
    raise SystemExit("could not find ClockFace enum before kNumFaces")
items = [item.strip() for item in body.group("body").split(",")]
faces = [item for item in items if item and not item.startswith("//")]
print(len(faces))
PY
)"

if [[ "${html_face_count}" != "${enum_face_count}" ]]; then
  echo "error: simulator exposes ${html_face_count} faces but ClockFace has ${enum_face_count}" >&2
  exit 1
fi

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"
cp "${BUILD_DIR}/astrolabe-web-sim.html" "${OUT_DIR}/index.html"
cp "${BUILD_DIR}/astrolabe-web-sim.js" "${OUT_DIR}/"
cp "${BUILD_DIR}/astrolabe-web-sim.wasm" "${OUT_DIR}/"
cp -R "${BUILD_DIR}/assets" "${OUT_DIR}/assets"

echo "${OUT_DIR}/index.html"
