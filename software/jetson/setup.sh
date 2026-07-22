#!/usr/bin/env bash
set -euo pipefail

python3 -m pip install --user --break-system-packages platformio
python3 -m pip install --user --break-system-packages -r "$(dirname "$0")/gui/requirements.txt"

if id -nG | tr ' ' '\n' | grep -qx dialout; then
  echo "dialout permission: OK"
else
  echo "dialout permission: MISSING"
  echo "Run this once on the Jetson, then reconnect/reboot:"
  echo "  sudo usermod -aG dialout \$USER"
  echo "  sudo reboot"
fi

echo "Build test:"
python3 -m platformio run -d "$(dirname "$0")/../../firmware/control"
