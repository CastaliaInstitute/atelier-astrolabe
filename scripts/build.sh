#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

pio run -e waveshare_s3_175

