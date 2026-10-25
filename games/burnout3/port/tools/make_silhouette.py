#!/usr/bin/env python3
"""A car select picture of our own for the EXTRA SERIES (port/extra_pictures/):
a car's silhouette with a question mark - drawn here, nothing of the game's.

    python make_silhouette.py out.png

128 x 64 RGBA, drawn at 4x and scaled down for smooth edges.
"""

from __future__ import annotations

import sys

from PIL import Image, ImageDraw, ImageFont

SCALE = 4
W, H = 128 * SCALE, 64 * SCALE
BODY = (38, 44, 58, 255)
EDGE = (120, 132, 156, 255)
MARK = (235, 235, 240, 255)


def font(size: int):
    for name in ("arialbd.ttf", "C:/Windows/Fonts/arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def main(argv) -> int:
    out = argv[1] if len(argv) > 1 else "silhouette.png"
    s = SCALE
    image = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    # A car side on: body, cabin, wheels - outlined, then filled.
    body = [(8 * s, 34 * s), (16 * s, 26 * s), (40 * s, 23 * s), (52 * s, 12 * s), (86 * s, 11 * s),
            (100 * s, 23 * s), (116 * s, 26 * s), (121 * s, 33 * s), (121 * s, 44 * s), (8 * s, 44 * s)]
    wheels = [(34 * s, 45 * s), (96 * s, 45 * s)]
    radius = 10 * s
    for grow, colour in ((2 * s, EDGE), (0, BODY)):
        d.polygon([(x + (grow if x > W / 2 else -grow), y - grow) for x, y in body], fill=colour)
        d.rounded_rectangle((8 * s - grow, 30 * s - grow, 121 * s + grow, 46 * s + grow), radius=5 * s, fill=colour)
        for cx, cy in wheels:
            d.ellipse((cx - radius - grow, cy - radius - grow, cx + radius + grow, cy + radius + grow), fill=colour)
    # The question mark in the middle.
    f = font(40 * s)
    box = d.textbbox((0, 0), "?", font=f)
    x = (W - (box[2] - box[0])) / 2 - box[0]
    y = (H - (box[3] - box[1])) / 2 - box[1] - 2 * s
    d.text((x, y), "?", font=f, fill=MARK, stroke_width=2 * s, stroke_fill=BODY)
    image.resize((128, 64), Image.LANCZOS).save(out)
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
