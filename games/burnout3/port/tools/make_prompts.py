"""The button prompts' keyboard and mouse art: the frontend's button icons
(the legend along the bottom of its screens, and wherever else the game draws
them) as keys and mouse buttons, for when the player uses the keyboard or the
mouse. The port puts them in place of the game's own textures as a texture
pack would (port/prompts.cpp).

Original art (no part of the game): each icon is drawn inside the box the
game's own icon fills in its texture - where the game's coordinates for it
point - so the game draws the new one in the old one's place. The boxes, and
the textures' names (as texture pack files name them), were taken from the
textures with PS2HOST_TEXTURE_DUMP. Drawn SCALE times larger than the
game's textures, from strokes (no font), white where the game's are coloured:
the legend tints them as it tints its own.

    python games/burnout3/port/tools/make_prompts.py [--preview out.png]

writes port/prompt_art.h: a PNG for each texture (alpha as the GS's, 0x80
opaque).
"""

import argparse
import io
import math
import os

from PIL import Image, ImageDraw

SCALE = 8            # the art, this many times the game's texture
SUPER = 4            # drawn this many times larger again, then reduced

WHITE = (255, 255, 255)
GREY = (180, 180, 180)           # the game's START word
DARK = (68, 69, 69)              # the game's dark Start and Select

# The textures (as texture pack files name them), their size, and the boxes
# of the icons in them (x0, y0, x1, y1, in the game's texels).
ATLAS = "6c432b9f02f261c7-1c2b765d5e998877-000019d3"      # 128 x 64: the face buttons, Start, Select
ARMS = "e2cabf8d5022260a-e76ee08bb6750c0f-00001154"       # 32 x 16: the D-pad's arms (down, right)
SHOULDER_L = "a0753932ac46bf20-758febaacdf3a257-00001593"  # 64 x 32: L1, L2
SHOULDER_R = "6c0ca9b9090026ff-c9b41b58a3740b4-00001593"   # 64 x 32: R1, R2

CROSS = (2, 3, 13, 14)
TRIANGLE = (16, 2, 27, 12)
CIRCLE = (1, 16, 14, 29)
SQUARE = (16, 17, 27, 28)
RING = (1, 33, 22, 54)                 # the disc every face button sits in
START = [(30, 1, 47, 15), (85, 40, 108, 57)]
START_DARK = [(49, 4, 60, 12), (86, 3, 105, 16)]
SELECT = (30, 18, 47, 30)
SELECT_DARK = (49, 20, 62, 28)
START_WORD = (67, 21, 125, 37)
ARM_UP = (2, 1, 14, 15)                # the game draws it as the down arm
ARM_LEFT = (16, 2, 30, 14)             # ...and this as the right
SHOULDERS = [(1, 4, 31, 28), (33, 4, 63, 28)]


# ------------------------------------------------------------------ glyphs
# Strokes in a unit box (x right, y down): lists of points, each a polyline.

def arc(cx, cy, rx, ry, a0, a1, steps=24):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]


GLYPHS = {
    "X": [[(0, 0), (1, 1)], [(1, 0), (0, 1)]],
    "V": [[(0, 0), (0.5, 1), (1, 0)]],
    "C": [arc(0.55, 0.5, 0.5, 0.5, -50, -310)],
    "Z": [[(0, 0), (1, 0), (0, 1), (1, 1)]],
    "Q": [arc(0.5, 0.48, 0.5, 0.48, 0, 360, 36), [(0.6, 0.65), (1.0, 1.05)]],
    "E": [[(1, 0), (0, 0), (0, 1), (1, 1)], [(0, 0.5), (0.8, 0.5)]],
    "1": [[(0.15, 0.25), (0.6, 0), (0.6, 1)]],
    "3": [arc(0.45, 0.27, 0.45, 0.27, -160, 90) + arc(0.45, 0.75, 0.47, 0.25, -90, 160)],
    "N": [[(0, 1), (0, 0), (1, 1), (1, 0)]],
    "T": [[(0, 0), (1, 0)], [(0.5, 0), (0.5, 1)]],
    "R": [[(0, 1), (0, 0), (0.6, 0)] + arc(0.6, 0.25, 0.38, 0.25, -90, 90) + [(0, 0.5)], [(0.5, 0.5), (1, 1)]],
}
# How wide each is, against its height.
WIDTH = {"1": 0.55, "C": 0.85, "Q": 0.95, "E": 0.75, "3": 0.8}


class Canvas:
    """A texture's art: drawn at SCALE * SUPER, in the game's texels."""

    def __init__(self, w, h):
        self.k = SCALE * SUPER
        self.image = Image.new("RGBA", (w * self.k, h * self.k), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.image)

    def p(self, x, y):
        return (x * self.k, y * self.k)

    def strokes(self, strokes, box, colour, width):
        """Unit-box strokes into `box` (x, y, w, h in texels), `width` texels thick, round ends."""
        bx, by, bw, bh = box
        r = width * self.k / 2
        for line in strokes:
            pts = [self.p(bx + x * bw, by + y * bh) for x, y in line]
            self.draw.line(pts, fill=colour, width=max(1, round(width * self.k)), joint="curve")
            for x, y in (pts[0], pts[-1]):
                self.draw.ellipse((x - r, y - r, x + r, y + r), fill=colour)

    def polygon(self, points, box, colour):
        bx, by, bw, bh = box
        self.draw.polygon([self.p(bx + x * bw, by + y * bh) for x, y in points], fill=colour)

    def keycap(self, cell, colour=WHITE, width=1.6, inset=1.0, radius=0.28):
        """A key's outline, a rounded square in the middle of the box."""
        x0, y0, x1, y1 = cell
        side = min(x1 - x0, y1 - y0) - 2 * inset
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        a, b = self.p(cx - side / 2 + width / 2, cy - side / 2 + width / 2), self.p(cx + side / 2 - width / 2,
                                                                                   cy + side / 2 - width / 2)
        self.draw.rounded_rectangle((a, b), radius=side * radius * self.k, outline=colour,
                                    width=round(width * self.k))

    def text(self, word, cell, colour, height_part=0.72, width=None):
        """Letters centred in the box, `height_part` of its height."""
        x0, y0, x1, y1 = cell
        h = (y1 - y0) * height_part
        gap = h * 0.22
        widths = [h * 0.78 * WIDTH.get(ch, 1.0) for ch in word]
        total = sum(widths) + gap * (len(word) - 1)
        if total > (x1 - x0) * 0.9:                      # too wide: smaller
            f = (x1 - x0) * 0.9 / total
            h, gap, widths, total = h * f, gap * f, [w * f for w in widths], total * f
        x = (x0 + x1) / 2 - total / 2
        y = (y0 + y1) / 2 - h / 2
        stroke = width or max(1.0, h * 0.17)
        for ch, w in zip(word, widths):
            self.strokes(GLYPHS[ch], (x + stroke / 2, y + stroke / 2, w - stroke, h - stroke), colour, stroke)
            x += w + gap

    def mouse(self, cell, button, colour=WHITE):
        """A mouse, its left (0) or right (1) button filled."""
        x0, y0, x1, y1 = cell
        h = (y1 - y0) * 0.86
        w = h * 0.66
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        left, top = cx - w / 2, cy - h / 2
        stroke = max(0.8, h * 0.09)
        split = top + h * 0.45
        k = self.k
        # The body's outline, and the line under the buttons and between them
        # (square ends, inside it).
        self.draw.rounded_rectangle((self.p(left + stroke / 2, top + stroke / 2),
                                     self.p(left + w - stroke / 2, top + h - stroke / 2)),
                                    radius=(w / 2 - stroke / 2) * k, outline=colour, width=round(stroke * k))
        self.draw.rectangle((self.p(left + stroke / 2, split - stroke / 2), self.p(left + w - stroke / 2,
                                                                                    split + stroke / 2)), fill=colour)
        self.draw.rectangle((self.p(cx - stroke / 2, top + stroke / 2), self.p(cx + stroke / 2, split)), fill=colour)
        # The button pressed, filled: the body's rounded top on its side, a
        # little in from the lines (they stay seen).
        gap = stroke * 1.15
        body = Image.new("L", self.image.size, 0)
        ImageDraw.Draw(body).rounded_rectangle((self.p(left + gap, top + gap), self.p(left + w - gap, top + h - gap)),
                                               radius=(w / 2 - gap) * k, fill=255)
        side = Image.new("L", self.image.size, 0)
        sx0, sx1 = (left, cx - gap + stroke) if button == 0 else (cx + gap - stroke, left + w)
        ImageDraw.Draw(side).rectangle((self.p(sx0, top), self.p(sx1, split - gap + stroke)), fill=255)
        from PIL import ImageChops
        self.image.paste(Image.new("RGBA", self.image.size, colour + (255,)), (0, 0), ImageChops.multiply(body, side))

    def png(self):
        small = self.image.resize((self.image.width // SUPER, self.image.height // SUPER), Image.LANCZOS)
        r, g, b, a = small.split()
        a = a.point(lambda v: (v * 128 + 127) // 255)       # the GS's 1.0 is 0x80
        out = io.BytesIO()
        Image.merge("RGBA", (r, g, b, a)).save(out, "PNG", optimize=True)
        return out.getvalue(), small


def enter(c, cell, colour, width):
    """The Enter key's bent arrow."""
    x0, y0, x1, y1 = cell
    s = min(x1 - x0, y1 - y0) * 0.8
    box = ((x0 + x1) / 2 - s / 2, (y0 + y1) / 2 - s / 2, s, s)
    c.strokes([[(0.85, 0.1), (0.85, 0.6), (0.2, 0.6)]], box, colour, width)
    c.polygon([(0.0, 0.6), (0.35, 0.3), (0.35, 0.9)], box, colour)


def backspace(c, cell, colour, width):
    """The Backspace key's arrow, pointing left."""
    x0, y0, x1, y1 = cell
    s = min(x1 - x0, y1 - y0) * 0.85
    box = ((x0 + x1) / 2 - s * 0.7, (y0 + y1) / 2 - s / 2, s * 1.4, s)
    c.strokes([[(1.0, 0.5), (0.3, 0.5)]], box, colour, width)
    c.polygon([(0.0, 0.5), (0.35, 0.15), (0.35, 0.85)], box, colour)


def atlas(mouse):
    c = Canvas(128, 64)
    c.keycap(RING, width=1.8)
    if mouse:
        c.mouse(CROSS, 0)
        c.mouse(TRIANGLE, 1)
    else:
        c.text("X", CROSS, WHITE, 0.78)
        c.text("V", TRIANGLE, WHITE, 0.82)
    c.text("C", CIRCLE, WHITE, 0.74)
    c.text("Z", SQUARE, WHITE, 0.78)
    for cell in START:
        enter(c, cell, WHITE, 1.5)
    for cell in START_DARK:
        enter(c, cell, DARK, 2.0)
    backspace(c, SELECT, WHITE, 1.5)
    backspace(c, SELECT_DARK, DARK, 2.0)
    c.text("ENTER", START_WORD, GREY, 0.85, width=1.7)
    return c


def arms():
    """The D-pad's arms as arrow keys, a key each with its arrow pointing
    out from the middle. The game's arms point in - the first piece is the
    down arm (flipped for up), the second the right (for left) - so the
    arrows here point down and right."""
    c = Canvas(32, 16)
    for cell, tri in ((ARM_UP, [(0.5, 0.82), (0.2, 0.38), (0.8, 0.38)]),
                      (ARM_LEFT, [(0.82, 0.5), (0.38, 0.2), (0.38, 0.8)])):
        x0, y0, x1, y1 = cell
        c.keycap(cell, width=1.4, inset=0.6, radius=0.25)
        s = min(x1 - x0, y1 - y0)
        box = ((x0 + x1) / 2 - s / 2, (y0 + y1) / 2 - s / 2, s, s)
        c.polygon(tri, box, WHITE)
    return c


def shoulders(keys):
    """L1/L2, R1/R2 as their keys: grey keys, the letter dark."""
    c = Canvas(64, 32)
    for cell, key in zip(SHOULDERS, keys):
        x0, y0, x1, y1 = cell
        k = c.k
        c.draw.rounded_rectangle((c.p(x0 + 0.5, y0 + 0.5), c.p(x1 - 0.5, y1 - 0.5)), radius=5 * k,
                                 fill=(28, 28, 28, 255))
        c.draw.rounded_rectangle((c.p(x0 + 2, y0 + 2), c.p(x1 - 2, y1 - 2)), radius=4 * k,
                                 fill=(150, 150, 150, 255))
        c.text(key, (x0 + 2, y0 + 2, x1 - 2, y1 - 2), (250, 250, 250), 0.66, width=2.4)
    return c


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="all of them on one picture, to look at")
    args = parser.parse_args()
    arts = [("kAtlasKeyboard", ATLAS, atlas(False)), ("kAtlasMouse", ATLAS, atlas(True)),
            ("kArrowKeys", ARMS, arms()), ("kKeysL", SHOULDER_L, shoulders("Q1")),
            ("kKeysR", SHOULDER_R, shoulders("E3"))]
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "prompt_art.h")
    lines = ["/* The button prompts' keyboard and mouse art, from port/tools/make_prompts.py",
             "   (original art): PNGs for the frontend's button textures, by their texture",
             "   pack names. Generated - edit the script. */",
             "#pragma once", "", "namespace prompt_art {", ""]
    previews = []
    for name, texture, canvas in arts:
        data, small = canvas.png()
        previews.append(small)
        lines.append("constexpr char %sName[] = \"%s\";" % (name, texture))
        lines.append("constexpr unsigned char %s[] = {" % name)
        for i in range(0, len(data), 20):
            lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 20]) + ",")
        lines.append("};")
        lines.append("")
    lines.append("}  // namespace prompt_art")
    with open(out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", os.path.normpath(out), sum(len(c.png()[0]) for _, _, c in arts), "bytes of PNG")
    if args.preview:
        w = max(p.width for p in previews)
        h = sum(p.height + 16 for p in previews)
        sheet = Image.new("RGB", (w, h), (40, 60, 110))
        y = 0
        for p in previews:
            r, g, b, a = p.split()
            sheet.paste(Image.merge("RGB", (r, g, b)), (0, y), a.point(lambda v: min(255, v * 2)))
            y += p.height + 16
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
