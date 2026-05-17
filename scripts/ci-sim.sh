#!/usr/bin/env bash
# CI: headless host simulator tests + HAL backend compile smoke.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Python host sim tests"
pip install -q -r sim/requirements.txt
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export PYGAME_HIDE_SUPPORT_PROMPT=1
python3 -m unittest discover -s sim/host_round -p 'test_*.py' -v

echo "==> HAL header compile (QEMU backend)"
CXXFLAGS=(
  -std=c++17
  -Wall
  -Wextra
  -Werror
  -Iinclude
  -Isim/qemu
  -DMYNAH_SIM_QEMU
  -DUNIT_TEST
)
g++ "${CXXFLAGS[@]}" -c sim/qemu/display_framebuffer.cpp -o /tmp/mynah_disp.o
g++ "${CXXFLAGS[@]}" -c sim/qemu/touch_scripted.cpp -o /tmp/mynah_touch.o
g++ "${CXXFLAGS[@]}" -c sim/qemu/imu_scripted.cpp -o /tmp/mynah_imu.o

echo "==> HAL layout checks"
test -f include/mynah_hal/mynah_hal.h
test -f devices/waveshare-1.75c/board_config.h
test -f sim/qemu/sdkconfig.qemu.defaults

echo "ci-sim: OK"
