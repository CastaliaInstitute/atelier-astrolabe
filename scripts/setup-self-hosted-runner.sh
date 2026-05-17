#!/usr/bin/env bash
# Register this Mac as github.com/CastaliaInstitute/astrolabe self-hosted runner (astrolabe-watch).
#
#   ./scripts/setup-self-hosted-runner.sh
#   cd ~/actions-runner-astrolabe && ./run.sh    # foreground
#
set -euo pipefail
REPO="${ASTROLABE_GH_REPO:-CastaliaInstitute/astrolabe}"
OWNER="${REPO%%/*}"
NAME="${REPO##*/}"
RUNNER_NAME="${ASTROLABE_RUNNER_NAME:-$(hostname -s | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-')-astrolabe}"
INSTALL_DIR="${ASTROLABE_RUNNER_DIR:-$HOME/actions-runner-astrolabe}"
LABELS="${ASTROLABE_RUNNER_LABELS:-self-hosted,astrolabe-watch,macOS}"
ARCH="$(uname -m)"
OS="osx"
[[ "$ARCH" == "arm64" ]] && PKG="actions-runner-osx-arm64" || PKG="actions-runner-osx-x64"
VER="2.321.0"
URL="https://github.com/actions/runner/releases/download/v${VER}/${PKG}-${VER}.tar.gz"

command -v gh >/dev/null || { echo "install gh first"; exit 1; }

if ! TOKEN="$(gh api -X POST "repos/${OWNER}/${NAME}/actions/runners/registration-token" --jq .token 2>/dev/null)"; then
  echo "error: cannot get registration token (need repo admin)." >&2
  echo "  GitHub → ${OWNER}/${NAME} → Settings → Actions → Runners → New self-hosted runner" >&2
  echo "  Copy the token, then:" >&2
  echo "    cd ${INSTALL_DIR} && ./config.sh --url https://github.com/${OWNER}/${NAME} --token <TOKEN> --labels ${LABELS} --name ${RUNNER_NAME}" >&2
  exit 1
fi
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"
if [[ ! -f bin/Runner.Listener ]]; then
  echo "→ download runner ${VER}"
  curl -fsSL "$URL" -o runner.tgz
  tar xzf runner.tgz && rm runner.tgz
fi

if [[ -f .runner ]]; then
  echo "Runner already configured in ${INSTALL_DIR} (remove .runner to re-register)"
  exit 0
fi

./config.sh \
  --url "https://github.com/${OWNER}/${NAME}" \
  --token "$TOKEN" \
  --name "$RUNNER_NAME" \
  --labels "$LABELS" \
  --unattended \
  --replace

cat <<EOF

Runner installed in: ${INSTALL_DIR}
Start (keep terminal open or use launchd):

  cd ${INSTALL_DIR} && ./run.sh

Secrets: set ASTROLABE_SECRETS_FILE (default ~/GitHub/astrolabe/include/secrets.local.h)
Optional: ASTROLABE_UPLOAD_PORT=/dev/cu.usbmodem101 in LaunchAgent (install-runner-launchagent.sh)

Enable integration gate: repo variable ENABLE_INTEGRATION_DEVICE_GATE=true
Remove stale offline runners in GitHub → Settings → Actions → Runners
EOF
