#!/usr/bin/env bash
# Install launchd agent so the astrolabe self-hosted runner starts at login.
#
#   ./scripts/install-runner-launchagent.sh
#   ./scripts/install-runner-launchagent.sh --uninstall
#
set -euo pipefail
INSTALL_DIR="${ASTROLABE_RUNNER_DIR:-$HOME/actions-runner-astrolabe}"
LABEL="institute.castalia.astrolabe-actions-runner"
PLIST="$HOME/Library/LaunchAgents/${LABEL}.plist"

if [[ "${1:-}" == "--uninstall" ]]; then
  launchctl bootout "gui/$(id -u)/${LABEL}" 2>/dev/null || true
  rm -f "$PLIST"
  echo "→ removed ${PLIST}"
  exit 0
fi

if [[ ! -x "${INSTALL_DIR}/run.sh" ]]; then
  echo "error: run ./scripts/setup-self-hosted-runner.sh first" >&2
  exit 1
fi

mkdir -p "$HOME/Library/LaunchAgents" "${INSTALL_DIR}/_diag"
cat >"$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>${LABEL}</string>
  <key>ProgramArguments</key>
  <array>
    <string>${INSTALL_DIR}/run.sh</string>
  </array>
  <key>WorkingDirectory</key>
  <string>${INSTALL_DIR}</string>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>${INSTALL_DIR}/_diag/launchd.out.log</string>
  <key>StandardErrorPath</key>
  <string>${INSTALL_DIR}/_diag/launchd.err.log</string>
</dict>
</plist>
EOF

launchctl bootout "gui/$(id -u)/${LABEL}" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
launchctl enable "gui/$(id -u)/${LABEL}"
echo "→ installed ${PLIST}"
