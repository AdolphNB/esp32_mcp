#!/usr/bin/env bash
# Regenerate data/fonts/ui24.bin.
#
# Requirements:
#   - node/npm (npx lv_font_conv)
#   - a CJK TTF/OTF. The default uses the system Droid Sans Fallback.
#
# Usage:
#   ./tools/gen_fonts.sh
#   FONT=/path/to/NotoSansCJKsc-Regular.otf ./tools/gen_fonts.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONT="${FONT:-/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf}"
OUT="$ROOT/data/fonts/ui24.bin"
CHARS="$(mktemp)"
trap 'rm -f "$CHARS"' EXIT

if [ ! -f "$FONT" ]; then
  echo "font not found: $FONT" >&2
  exit 1
fi

python3 - "$CHARS" <<'PY'
import sys

chars = []
# GB2312 level 1 (3755 common hanzi)
for hi in range(0xB0, 0xD8):
    for lo in range(0xA1, 0xFF):
        try:
            c = bytes([hi, lo]).decode('gb2312')
        except UnicodeDecodeError:
            continue
        if c not in chars:
            chars.append(c)

extra = '，。！？；：“”‘’（）《》、…—·【】￥%+-=/\\|@#&*_~`^<>[]{}'
for c in extra:
    if c not in chars:
        chars.append(c)

with open(sys.argv[1], 'w', encoding='utf-8') as f:
    f.write(''.join(chars))
PY

mkdir -p "$(dirname "$OUT")"
npx --yes lv_font_conv@1.5.2 \
  --font "$FONT" \
  --size 24 \
  --bpp 4 \
  --format bin \
  --no-compress \
  -r 0x20-0x7F \
  --symbols "$(cat "$CHARS")" \
  -o "$OUT"

ls -lh "$OUT"
