#!/usr/bin/env bash
# ============================================================================
#  deploy.sh — build, flash (USB-C), and monitor XIAO ESP32-S3 demo projects
#
#  Usage:
#    ./deploy.sh                    flash mecanum bot + open serial monitor
#    ./deploy.sh build              compile mecanum bot only
#    ./deploy.sh upload             flash mecanum bot, no monitor
#    ./deploy.sh monitor            serial monitor only
#    ./deploy.sh esc build          compile the older ESC tester
#    ./deploy.sh ports              list detected USB serial ports
#
#  Exit the serial monitor with: Ctrl-C   (PlatformIO uses Ctrl-C to quit)
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

target="${DEMO_TARGET:-mecanum}"
cmd="${1:-all}"
case "${1:-}" in
  mecanum|drive|bot)
    target="mecanum"
    cmd="${2:-all}"
    ;;
  esc|tester|firmware)
    target="esc"
    cmd="${2:-all}"
    ;;
esac

case "$target" in
  mecanum) PROJ="$HERE/examples/mecanum-drive" ;;
  esc)     PROJ="$HERE/examples/esc-bench" ;;
  *)
    echo "Unknown demo target: $target"
    echo "Try: mecanum | esc"
    exit 1
    ;;
esac

# --- locate or install PlatformIO Core (pio) --------------------------------
find_pio() {
  for c in pio "$HOME/.platformio/penv/bin/pio" "$(command -v pio 2>/dev/null || true)"; do
    [ -n "${c:-}" ] && command -v "$c" >/dev/null 2>&1 && { echo "$c"; return 0; }
  done
  return 1
}

PIO="$(find_pio || true)"
if [ -z "${PIO:-}" ]; then
  echo "PlatformIO not found — installing..."
  if command -v brew >/dev/null 2>&1; then
    brew install platformio
  elif command -v pipx >/dev/null 2>&1; then
    pipx install platformio
  else
    python3 -m pip install --user platformio --break-system-packages || \
      python3 -m pip install --user platformio
  fi
  PIO="$(find_pio)" || { echo "Install failed. Try: brew install platformio"; exit 1; }
fi
echo "Using PlatformIO: $PIO"
echo "Project: $target ($PROJ)"

# --- detect the XIAO serial port (macOS) ------------------------------------
detect_port() {
  ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null | head -n1
}

case "$cmd" in
  ports)
    echo "Detected USB serial ports:"
    ls /dev/cu.* 2>/dev/null | grep -vi bluetooth | grep -vi debug-console || echo "  (none)"
    "$PIO" device list || true
    ;;
  build)
    "$PIO" run -d "$PROJ"
    ;;
  upload)
    PORT="$(detect_port || true)"
    [ -n "${PORT:-}" ] && echo "Port: $PORT" || echo "No port auto-detected; PlatformIO will guess."
    "$PIO" run -d "$PROJ" -t upload ${PORT:+--upload-port "$PORT"}
    ;;
  monitor)
    PORT="$(detect_port || true)"
    "$PIO" device monitor -d "$PROJ" -b 115200 ${PORT:+-p "$PORT"}
    ;;
  all|"")
    PORT="$(detect_port || true)"
    if [ -z "${PORT:-}" ]; then
      echo "!! No XIAO serial port found under /dev/cu.usbmodem*"
      echo "   - plug the XIAO into USB-C"
      echo "   - if first flash fails: hold BOOT, tap RESET, release BOOT, re-run"
    else
      echo "Port: $PORT"
    fi
    "$PIO" run -d "$PROJ" -t upload ${PORT:+--upload-port "$PORT"}
    echo "--- opening serial monitor (Ctrl-C to exit) ---"
    "$PIO" device monitor -d "$PROJ" -b 115200 ${PORT:+-p "$PORT"}
    ;;
  *)
    echo "Unknown command: $cmd"
    echo "Try: build | upload | monitor | ports"
    echo "Targets: mecanum (default) | esc"
    exit 1
    ;;
esac
