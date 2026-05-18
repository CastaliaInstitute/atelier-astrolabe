#!/usr/bin/env bash
# Schedule daily comprehensive bench on m1 (pull integration, flash, test).
#
#   ./scripts/install-device-bench-launchagent.sh
#   ./scripts/install-device-bench-launchagent.sh --uninstall
#   ./scripts/install-device-bench-launchagent.sh --run-now
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LABEL="institute.castalia.astrolabe-device-bench"
PLIST="$HOME/Library/LaunchAgents/${LABEL}.plist"
BENCH="${ROOT}/scripts/device-bench.sh"
HOUR="${ASTROLABE_BENCH_HOUR:-6}"

if [[ "${1:-}" == "--uninstall" ]]; then
  launchctl bootout "gui/$(id -u)/${LABEL}" 2>/dev/null || true
  rm -f "$PLIST"
  echo "→ removed ${PLIST}"
  exit 0
fi

if [[ "${1:-}" == "--run-now" ]]; then
  exec "$BENCH"
fi

mkdir -p "$HOME/Library/LaunchAgents" "${ROOT}/artifacts/bench"
cat >"$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>${LABEL}</string>
  <key>ProgramArguments</key>
  <array>
    <string>${BENCH}</string>
  </array>
  <key>WorkingDirectory</key>
  <string>${ROOT}</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>ASTROLABE_SECRETS_FILE</key>
    <string>${ASTROLABE_SECRETS_FILE:-$HOME/GitHub/astrolabe/include/secrets.local.h}</string>
    <key>ASTROLABE_USB_POWER_CYCLE</key>
    <string>1</string>
    <key>ASTROLABE_UHUBCTL_SEARCH</key>
    <string>Espressif</string>
    <key>PATH</key>
    <string>${HOME}/.astrolabe-ci-venv/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
  </dict>
  <key>StartCalendarInterval</key>
  <dict>
    <key>Hour</key>
    <integer>${HOUR}</integer>
    <key>Minute</key>
    <integer>0</integer>
  </dict>
  <key>StandardOutPath</key>
  <string>${ROOT}/artifacts/bench/launchd.out.log</string>
  <key>StandardErrorPath</key>
  <string>${ROOT}/artifacts/bench/launchd.err.log</string>
</dict>
</plist>
EOF

launchctl bootout "gui/$(id -u)/${LABEL}" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
launchctl enable "gui/$(id -u)/${LABEL}"
echo "→ installed ${PLIST} (daily at ${HOUR}:00)"
echo "→ logs ${ROOT}/artifacts/bench/launchd.{out,err}.log"
echo "→ run now: ${BENCH}"
