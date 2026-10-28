#!/usr/bin/env python3
"""Generate the boot splash logo for the 4.3B display.

Inputs:
  data/brand/brand-mark.svg   company mark (from singularitynear.com)

Outputs:
  data/brand/logo_boot.png             preview (RGBA, background baked in)
  components/app_ui/assets/logo_boot.bin   raw little-endian RGB565 (460x150)

The RGB565 blob is embedded into the firmware by components/app_ui/CMakeLists.txt.

Requirements:
  - Pillow
  - node/npm (npx sharp-cli is used to rasterize the SVG)
  - Noto Sans CJK + Noto Sans (override with CJK_FONT / LATIN_FONT env vars)
"""

import os
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "data", "brand", "brand-mark.svg")
PREVIEW = os.path.join(ROOT, "data", "brand", "logo_boot.png")
OUT_BIN = os.path.join(ROOT, "components", "app_ui", "assets", "logo_boot.bin")

WIDTH, HEIGHT = 460, 150
BG = (11, 20, 27)        # #0B141B
WHITE = (232, 243, 243)  # #E8F3F3
TEAL = (84, 214, 199)    # #54D6C7

CJK_FONT = os.environ.get("CJK_FONT", "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc")
LATIN_FONT = os.environ.get("LATIN_FONT", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf")


def rasterize_mark(size=256):
    png = "/tmp/lvgl_boot_mark.png"
    subprocess.run(
        ["npx", "--yes", "sharp-cli", "-i", SVG, "-o", png, "resize", str(size), str(size)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return Image.open(png).convert("RGBA")


def draw_spaced(draw, xy, text, font, fill, spacing):
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += int(draw.textlength(ch, font=font)) + spacing


def main():
    mark = rasterize_mark()
    img = Image.new("RGBA", (WIDTH, HEIGHT), BG + (255,))
    img.alpha_composite(mark.resize((118, 118), Image.LANCZOS), (6, 16))

    draw = ImageDraw.Draw(img)
    cjk = ImageFont.truetype(CJK_FONT, 52, index=2)
    draw.text((148, 22), "奇点临近", font=cjk, fill=WHITE)

    latin = ImageFont.truetype(LATIN_FONT, 17)
    draw_spaced(draw, (150, 100), "SINGULARITY NEAR", latin, TEAL, 5)

    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    img.save(PREVIEW)

    rgb = img.convert("RGB")
    data = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = rgb.getpixel((x, y))
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data += bytes((v & 0xFF, (v >> 8) & 0xFF))
    with open(OUT_BIN, "wb") as f:
        f.write(data)

    print(f"preview: {PREVIEW} ({WIDTH}x{HEIGHT})")
    print(f"blob:    {OUT_BIN} ({len(data)} bytes)")


if __name__ == "__main__":
    sys.exit(main())
