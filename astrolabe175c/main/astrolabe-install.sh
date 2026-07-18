#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET_DIR="/opt/astrolabe"
AUTOSTART_DIR="${HOME}/.config/autostart"

if [[ ! -f "${SOURCE_DIR}/astrolabe_pi_screen.py" ]]; then
  echo "Astrolabe installer: astrolabe_pi_screen.py is missing from the USB drive" >&2
  exit 1
fi

echo "Astrolabe installer: installing Raspberry Pi screen agent"
sudo install -d -m 0755 "${TARGET_DIR}"
sudo install -m 0755 "${SOURCE_DIR}/astrolabe_pi_screen.py" "${TARGET_DIR}/astrolabe_pi_screen.py"
sudo apt-get update
sudo apt-get install -y python3-pil python3-serial python3-mss grim

sudo tee /etc/udev/rules.d/70-astrolabe-km.rules >/dev/null <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="8003", GROUP="plugdev", MODE="0660", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty

install -d -m 0755 "${AUTOSTART_DIR}"
cat >"${AUTOSTART_DIR}/astrolabe-screen.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Astrolabe USB Screen
Comment=Stream the Raspberry Pi desktop to Astrolabe
Exec=sh -c 'port=$(readlink -f /dev/serial/by-id/*Astrolabe* 2>/dev/null | head -1); [ -n "$port" ] || port=/dev/ttyACM0; exec /usr/bin/python3 /opt/astrolabe/astrolabe_pi_screen.py --usb "$port" --fps 4'
Terminal=false
X-GNOME-Autostart-enabled=true
EOF

echo "Astrolabe installer: installed. Starting the screen agent now."
port="$(readlink -f /dev/serial/by-id/*Astrolabe* 2>/dev/null | head -1 || true)"
[[ -n "${port}" ]] || port=/dev/ttyACM0
nohup /usr/bin/python3 "${TARGET_DIR}/astrolabe_pi_screen.py" --usb "${port}" --fps 4 \
  >"${HOME}/.cache/astrolabe-screen.log" 2>&1 &
disown || true
echo "Astrolabe installer: complete"
