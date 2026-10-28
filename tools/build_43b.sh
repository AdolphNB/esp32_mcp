#!/usr/bin/env bash
# Build the Waveshare ESP32-S3-Touch-LCD-4.3B firmware into build-43b/.
#
# This intentionally uses its own build directory and sdkconfig so it cannot
# accidentally pick up the YD-ESP32-S3 node configuration from ./build/.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [ -z "${IDF_PATH:-}" ]; then
  echo "IDF_PATH is not set. Run: source ~/.espressif/tools/activate_idf_v6.1.sh" >&2
  exit 1
fi
IDF_PY="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/tools/python/v6.1/venv}/bin/python"
IDF_TOOL="$IDF_PATH/tools/idf.py"

rm -f build-43b/sdkconfig

"$IDF_PY" "$IDF_TOOL" -B build-43b \
  -D SDKCONFIG=build-43b/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.display" \
  build

echo
echo "Built: $ROOT/build-43b/esp32_mcp.bin"
echo "Flashing (full flash, includes storage/font):"
echo "  ./tools/flash_43b.sh /dev/ttyACM0"
