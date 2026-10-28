#!/usr/bin/env bash
# Regenerate data/fonts/ui24.bin.
#
# Requirements:
#   - node/npm (npx lv_font_conv)
#   - a CJK TTF/OTF. The default uses the system Droid Sans Fallback.
#   - a Latin TTF/OTF for ASCII. The default uses Noto Sans (fallback: DejaVu Sans).
#
# Usage:
#   ./tools/gen_fonts.sh
#   FONT=/path/to/NotoSansCJKsc-Regular.otf ./tools/gen_fonts.sh
#   LATIN_FONT=/path/to/NotoSans-Regular.ttf ./tools/gen_fonts.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONT="${FONT:-/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf}"
if [ -n "${LATIN_FONT:-}" ]; then
  :
elif [ -f /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf ]; then
  LATIN_FONT=/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf
else
  LATIN_FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
fi
OUT="$ROOT/data/fonts/ui24.bin"
CHARS="$(mktemp)"
trap 'rm -f "$CHARS"' EXIT

if [ ! -f "$FONT" ]; then
  echo "CJK font not found: $FONT" >&2
  exit 1
fi
if [ ! -f "$LATIN_FONT" ]; then
  echo "Latin font not found: $LATIN_FONT" >&2
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

# Only non-ASCII punctuation here: ASCII punctuation/letters/digits come from
# the Latin font. Requesting the same codepoint from both fonts can make
# lv_font_conv drop it when the CJK font has no glyph for it.
extra = '，。！？；：（）《》、【】￥'
for c in extra:
    if c not in chars:
        chars.append(c)

with open(sys.argv[1], 'w', encoding='utf-8') as f:
    f.write(''.join(chars))
PY

mkdir -p "$(dirname "$OUT")"
# Two fonts are merged into one 24px font:
#   - Latin/ASCII (including the space glyph) from the Latin font
#   - GB2312 hanzi + CJK punctuation from the CJK font
# This keeps mixed text like "在 PC 上" at one size and guarantees that
# spaces are real space glyphs instead of the not-found placeholder.
npx --yes lv_font_conv@1.5.2 \
  --size 24 \
  --bpp 4 \
  --format bin \
  --no-compress \
  --font "$LATIN_FONT" -r 0x20-0x7E -r 0xA0-0xFF -r 0x2000-0x206F \
  --font "$FONT" --symbols "$(cat "$CHARS")" \
  -o "$OUT"

ls -lh "$OUT"
