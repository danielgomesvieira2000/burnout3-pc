#!/usr/bin/env python3
"""The EXTRA SERIES' cars' pictures for the car select's column, as the game
keeps a car's (DATA/FRONTEND.TXD, a record named by the car's ID): a 128 x 64
picture of 8-bit indices and eight palettes of 256 colours (one per paint
colour), each sent to the GS as a 64 x 32 32-bit image - its words as the GS
lays out an 8-bit texture, the palettes as 16 x 16 tiles (CSM1). The port
lays the records over the disc (extra_series.cpp); this makes their two
images from port/extra_pictures/<ID>.png (128 x 64 RGBA) at build time.

    python extra_pictures.py --pictures port/extra_pictures --out build/extra_pictures.cpp

No pictures there: an empty list (the column then shows the game's own
warning sign for those cars).
"""

from __future__ import annotations

import argparse
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "engine", "tools"))
import gsmem  # noqa: E402

WIDTH, HEIGHT = 128, 64
PALETTES = 8


def upload(mem: bytearray, bp: int) -> bytes:
    """The 64 x 32 32-bit image at block `bp` as it is sent: rows of words."""
    out = bytearray()
    for y in range(32):
        for x in range(64):
            a = gsmem.address(bp, 1, x, y, 0x00)
            out += mem[a:a + 4]
    return bytes(out)


def encode(path: str) -> tuple:
    from PIL import Image
    image = Image.open(path).convert("RGBA")
    if image.size != (WIDTH, HEIGHT):
        raise SystemExit("%s: %dx%d, not %dx%d" % (path, image.size[0], image.size[1], WIDTH, HEIGHT))
    # 255 colours for what shows, entry 0 the transparent one.
    pixels = list(image.getdata())
    shown = Image.new("RGBA", image.size)
    shown.putdata([p if p[3] else (0, 0, 0, 0) for p in pixels])
    quantized = shown.quantize(colors=255, method=Image.Quantize.FASTOCTREE)
    palette = quantized.getpalette(rawmode="RGBA")[:4 * 255]
    colours = [(0, 0, 0, 0)] + [tuple(palette[4 * i:4 * i + 4]) for i in range(len(palette) // 4)]
    colours += [(0, 0, 0, 0)] * (256 - len(colours))
    indices = [0 if p[3] == 0 else 1 + q for p, q in zip(pixels, quantized.getdata())]
    # The GS's memory: the indices as an 8-bit texture (block 0, 128 wide),
    # the palettes as a 32-bit image beside them (block 32, 64 wide).
    mem = bytearray(64 * 256 * 2)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            mem[gsmem.address8(0, 2, x, y)] = indices[y * WIDTH + x]
    for k in range(PALETTES):
        tx, ty = (k % 4) * 16, (k // 4) * 16
        for i, (r, g, b, a) in enumerate(colours):
            p = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)      # CSM1's order
            at = gsmem.address(32, 1, tx + p % 16, ty + p // 16, 0x00)
            mem[at:at + 4] = bytes((r, g, b, (a * 128 + 127) // 255))   # the GS's alpha: 0-128
    return upload(mem, 0), upload(mem, 32)


def array(data: bytes) -> str:
    return ",".join("0x%02X" % b for b in data)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pictures", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    entries = []
    lines = ["// Made by games/burnout3/port/tools/extra_pictures.py: the EXTRA SERIES' cars' pictures.",
             "#include \"ps2rt/types.h\"", "struct ExtraPicture { const char* id; const u8* indices; const u8* palettes; };"]
    for path in sorted(glob.glob(os.path.join(args.pictures, "*.png"))):
        name = os.path.splitext(os.path.basename(path))[0].upper()
        indices, palettes = encode(path)
        lines.append("static const u8 k%sIndices[] = {%s};" % (name, array(indices)))
        lines.append("static const u8 k%sPalettes[] = {%s};" % (name, array(palettes)))
        entries.append("{\"%s\", k%sIndices, k%sPalettes}" % (name, name, name))
    lines.append("extern const ExtraPicture kExtraPictures[] = {%s};" % ", ".join(entries + ["{nullptr, nullptr, nullptr}"]))
    text = "\n".join(lines) + "\n"
    if not os.path.exists(args.out) or open(args.out, encoding="utf-8").read() != text:
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    print("extra pictures: %d" % len(entries))
    return 0


if __name__ == "__main__":
    sys.exit(main())
