#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' "$ROOT/CMakeLists.txt" | head -n1)}"
VERSION="${VERSION:-0.1.0}"
PORT="${2:-8000}"
BUILD_BIN="$ROOT/build/esp32_mcp.bin"
OUT_DIR="${HOME}/esp32-fw"
OUT_BIN="$OUT_DIR/esp32_mcp-${VERSION}.bin"

if [[ ! -f "$BUILD_BIN" ]]; then
  echo "missing $BUILD_BIN; run 'idf.py build' first" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
cp "$BUILD_BIN" "$OUT_BIN"
SHA="$(sha256sum "$OUT_BIN" | awk '{print $1}')"
SIZE="$(stat -c %s "$OUT_BIN")"
HOST_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
[[ -z "$HOST_IP" ]] && HOST_IP="<this-host-ip>"

cat > "$OUT_DIR/manifest.json" <<EOF
{
  "version": "$VERSION",
  "file": "$(basename "$OUT_BIN")",
  "size": $SIZE,
  "sha256": "$SHA",
  "url": "http://$HOST_IP:$PORT/$(basename "$OUT_BIN")"
}
EOF

cat <<EOF
Firmware ready
  file:   $OUT_BIN
  sha256: $SHA
  size:   $SIZE bytes
  url:    http://$HOST_IP:$PORT/$(basename "$OUT_BIN")

Serve with:
  python3 -m http.server $PORT --bind 0.0.0.0 --directory "$OUT_DIR"

Device Kconfig:
  CONFIG_APP_OTA_ALLOWED_URL_PREFIX="http://$HOST_IP:$PORT/"

Agent tool call:
  {"name":"ota_apply","arguments":{"url":"http://$HOST_IP:$PORT/$(basename "$OUT_BIN")",
   "sha256":"$SHA","version":"$VERSION","confirm":"OTA"}}

EOF

exec python3 -m http.server "$PORT" --bind 0.0.0.0 --directory "$OUT_DIR"
