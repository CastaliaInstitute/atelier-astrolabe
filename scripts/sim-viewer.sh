#!/usr/bin/env bash
# Launch Level-1 round host viewer (optional BMP from QA artifacts).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "${ROOT}/sim/host_round/viewer.py" "$@"
