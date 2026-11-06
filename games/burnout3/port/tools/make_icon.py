"""A player's own icon for their build, from their disc: the title screen's
BURNOUT 3 TAKEDOWN logo, as the game draws it. EA's artwork: for the player's
build alone, never committed or shared - the port's own icon is original
(port/icon.ico, from make_icon_art.py).

The built port runs to the title screen (a scripted run, as the regressions
are), the frame's GS memory is dumped and its trace read: the logo is a
512 x 256 8-bit texture drawn across the middle of the screen. Decoded with
its CLUT (engine/tools/gsmem.py), it becomes the icon: the whole logo at the
large sizes, its red 3 alone at the small ones, where the words would not
read. Written to build/port/icon.ico, which ps2_add_port embeds instead of
the port's own at the next build; delete it to have the port's own back.

    python games/burnout3/port/tools/make_icon.py
"""

import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
ROOT = os.path.normpath(os.path.join(GAME, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "engine", "tools"))
import gsmem  # noqa: E402

FRAME = 3900                # the title screen, its logo still
PRESSES = ["--press", "up@1200", "--press", "cross@1350", "--press", "start@3700"]
LARGE = (256, 128, 64)      # the whole logo
SMALL = (48, 32, 24, 16)    # the 3


def title_screen(work):
    """The title frame's GS memory and trace, from a scripted run."""
    exe = os.path.join(GAME, "build", "port", "cmake", "burnout3.exe")
    if not os.path.exists(exe):                 # a Mac's (engine/tools/macos.py build)
        exe = os.path.join(GAME, "build", "macos", "burnout3", "burnout3")
    elf = os.path.join(GAME, "extracted", "SLUS_210.50")
    with open(os.path.join(GAME, "game.json"), encoding="utf-8") as f:
        iso = os.path.normpath(os.path.join(GAME, re.search(r'"iso":\s*"([^"]+)"', f.read()).group(1)))
    args = [exe, elf, "--disc", iso, "--virtual-time"] + PRESSES + [
        "--until-frame", str(FRAME + 2), "--gs-trace-frame", str(FRAME), "--gs-dump-frame", str(FRAME),
        "--frames", work, "--timeout", "900"]
    with open(os.path.join(work, "trace.txt"), "w") as out:
        subprocess.run(args, stdout=out, stderr=subprocess.STDOUT, check=False)
    return os.path.join(work, "gsmem_%d.bin" % FRAME), os.path.join(work, "trace.txt")


def logo_texture(trace):
    """TEX0 of the logo: an 8-bit 512 x 256 texture drawn wider than 400."""
    pattern = re.compile(r"tex0 tbp (\w+) tbw (\d+) psm 13 512x256 cbp (\w+) .*?\| \(([\d.]+),[\d.]+\)-\(([\d.]+),")
    with open(trace) as f:
        for line in f:
            m = pattern.search(line)
            if m and float(m.group(5)) - float(m.group(4)) > 400:
                return int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)
    raise SystemExit("make_icon: no logo drawn at frame %d (see %s)" % (FRAME, trace))


def the_three(logo):
    """The logo's red 3 alone: the solid red shape at its top right (taller
    than the other letters), and its outline around it."""
    w, h = logo.size
    px = logo.load()
    solid = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            solid[y][x] = a > 100 and r > 70 and r > g + 25
    tallest = [x for x in range(w) if any(px[x, y][3] > 60 for y in range(int(h * 0.12)))]
    seed = next((x, y) for y in range(int(h * 0.2), int(h * 0.5)) for x in range(min(tallest), w) if solid[y][x])
    keep = Image.new("L", (w, h), 0)
    kp = keep.load()
    stack = [seed]
    while stack:
        x, y = stack.pop()
        if 0 <= x < w and 0 <= y < h and solid[y][x] and not kp[x, y]:
            kp[x, y] = 255
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    grown = keep.filter(ImageFilter.MaxFilter(9))
    three = logo.copy()
    three.putalpha(Image.composite(logo.getchannel("A"), Image.new("L", (w, h), 0), grown))
    return three.crop(three.getbbox())


def square(image, size, pad):
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    scale = min(size * (1 - 2 * pad) / image.width, size * (1 - 2 * pad) / image.height)
    fitted = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS)
    canvas.alpha_composite(fitted, ((size - fitted.width) // 2, (size - fitted.height) // 2))
    return canvas


def write_ico(images, path):
    """An .ico of PNG images (Windows Vista on reads them at every size)."""
    blobs = []
    for image in images:
        data = io.BytesIO()
        image.save(data, "PNG")
        blobs.append(data.getvalue())
    offset = 6 + 16 * len(images)
    with open(path, "wb") as out:
        out.write(struct.pack("<HHH", 0, 1, len(images)))
        for image, blob in zip(images, blobs):
            side = image.width % 256
            out.write(struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(blob), offset))
            offset += len(blob)
        for blob in blobs:
            out.write(blob)


def main():
    work = tempfile.mkdtemp(prefix="b3icon_")
    try:
        dump, trace = title_screen(work)
        if not os.path.exists(dump):
            raise SystemExit("make_icon: the run made no GS dump (see %s)" % trace)
        tbp, tbw, cbp = logo_texture(trace)
        rows = gsmem.texture8(open(dump, "rb").read(), tbp, tbw, 512, 256, cbp)
        texture = Image.frombytes("RGBA", (512, 256), bytes(c for row in rows for px in row for c in px))
        logo = texture.crop(texture.getbbox())
        three = the_three(logo)
        images = [square(logo, s, 0.02) for s in LARGE] + [square(three, s, 0.04) for s in SMALL]
        out = os.path.join(GAME, "build", "port", "icon.ico")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        write_ico(images, out)
        print("make_icon: %s (the logo from TEX0 %X, CLUT %X)" % (out, tbp, cbp))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
