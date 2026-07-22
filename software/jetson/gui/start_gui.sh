#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if pgrep -f "python3 -m uvicorn openbot_gui:app" >/dev/null; then
  echo "OpenBot GUI is already running."
  exit 0
fi

nohup python3 -m uvicorn openbot_gui:app --host 0.0.0.0 --port 8000 > openbot_gui.log 2>&1 < /dev/null &
echo "OpenBot GUI started on port 8000."
