#!/usr/bin/env bash
# Flash the 4.3B build from build-43b/.
#
# Usage:
#   ./tools/flash_43b.sh [/dev/ttyACM0] [--erase]
#
# --erase wipes NVS (Wi-Fi/token) and the OTA state. Useful for first bring-up
# or when an old OTA slot keeps booting.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PORT="${1:-/dev/ttyACM0}"
ERASE="${2:-}"

if [ -z "${IDF_PATH:-}" ]; then
  echo "IDF_PATH is not set. Run: source ~/.espressif/tools/activate_idf_v6.1.sh" >&2
  exit 1
fi
IDF_PY="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/tools/python/v6.1/venv}/bin/python"
IDF_TOOL="$IDF_PATH/tools/idf.py"

if [ ! -f build-43b/sdkconfig ]; then
  echo "build-43b/sdkconfig not found; run ./tools/build_43b.sh first" >&2
  exit 1
fi
if ! grep -q '^CONFIG_APP_BOARD_WAVESHARE_43B=y' build-43b/sdkconfig; then
  echo "build-43b is not configured for the 4.3B board" >&2
  echo "run ./tools/build_43b.sh first" >&2
  exit 1
fi
if [ ! -f build-43b/esp32_mcp.bin ]; then
  echo "build-43b/esp32_mcp.bin not found; run ./tools/build_43b.sh first" >&2
  exit 1
fi

SIZE="$(stat -c%s build-43b/esp32_mcp.bin)"
if [ "$SIZE" -lt 1800000 ]; then
  echo "warning: app is only $SIZE bytes; this looks like the old YD build" >&2
  echo "run ./tools/build_43b.sh first" >&2
  exit 1
fi

if [ ! -e "$PORT" ]; then
  echo "serial port not found: $PORT" >&2
  echo "available:" >&2
  ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
  exit 1
fi

if [ "$ERASE" = "--erase" ]; then
  echo "Erasing flash on $PORT (Wi-Fi/token will be lost)..."
  "$IDF_PY" "$IDF_TOOL" -B build-43b -p "$PORT" erase-flash
fi

echo "Flashing build-43b ($SIZE bytes) to $PORT..."
"$IDF_PY" "$IDF_TOOL" -B build-43b -p "$PORT" flash

echo
echo "Done. Watch the first boot with:"
echo "  \"$IDF_PY\" \"$IDF_TOOL\" -B build-43b -p $PORT monitor"
echo
echo "Expected first lines:"
echo "  board: board=waveshare_43b"
echo "  ui_lvgl: boot screen shown"
echo "  ui_lvgl: RGB+LVGL ready ..."
