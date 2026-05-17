#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

python3 -m platformio run -e waveshare_s3_175
