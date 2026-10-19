#!/usr/bin/env python3
"""Lift the game's functions as PC SETTINGS' MODS change them, beside the originals.

    python games/burnout3/port/tools/lift_mods.py --out build/port/cmake/mods_lifted.cpp

A mod is a PCSX2 patch (Nehalem's, github.com/Nahelam/PS2-Game-Mods, Burnout 3
Takedown/Various, SLUS-21050): words written over the game's code, and code of
its own written to free memory. The port runs the game's code translated, not
the code in memory, so a word written there would do nothing: each function a
mod changes is lifted again from the executable with the mod's words written
in - once for each combination of the mods that change it - and a mod's own
code is lifted from its words. port/mods.cpp chooses among them as the
settings say. The output is the game's code, like src/lifted/: generated at
build time from the player's disc, never committed.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "engine"))
sys.path[:0] = [os.path.join(ENGINE, "tools"), ENGINE]

from ps2decomp import game as game_config, lift  # noqa: E402
from lift import _other_sections  # noqa: E402  (engine/tools/lift.py)

# The mods, in the order their words go in where two change the same word (a
# later one's wins): a setting's key, and its words - (address, value, bytes).
# A key of "always" is no setting: the port always has it.
MODS = [
    # The speedometer in km/h: the scale from miles (gp-0x7ED8) to kilometres
    # (gp-0x7D10), and the unit's string (0x7D8 MPH, 0x7D7 KPH).
    ("mod.kph", [(0x001A3BA0, 0x82F0, 2), (0x001A3D78, 0x07D7, 2)]),
    # The horn's volume (0 in the game) 1.0: Circle (and R3) honks.
    ("mod.horn", [(0x0028CDD0, 0x3C023F80, 4), (0x0028CDE8, 0xAFA2005C, 4),
                  # The car's lights (+0x58 of its +0xCC0) on while the horn is held
                  # (+0x13FC bit 3): a call to the mod's code (0x000B40C0) in place
                  # of the store that turns them off.
                  (0x001C4934, 0x0C02D030, 4)]),
    # (Mirror mode, the patch's 0x0019EFA4 sb s2 - its caller's s2, whatever that
    # holds - is port/mods.cpp's: a 1 written after the function.)
    # Crash Breaker in every mode (the race's R2): its test for crash mode
    # gone, and the R2 that sets it off read in every mode.
    ("mod.crash_breaker", [(0x001BB9B8, 0x00000000, 4), (0x001BFCAC, 0x2C510001, 4),
                           (0x001BFCBC, 0xA2514586, 4), (0x001BFCC0, 0x92450218, 4)]),
    # Every race car's headlights on (after the horn's: on, held or not).
    ("mod.headlights", [(0x001C4930, 0x8E430CC0, 4), (0x001C4934, 0x24040004, 4),
                        (0x001C4940, 0xA0640058, 4)]),
    # More particles while driving (GRAPHICS' PARTICLES; Nehalem's "Render
    # extra particles while driving"): the particles' update (0x00261E00) asks
    # for its extra ones (0x00254A50's a0) always - MORE, "partial" - and also
    # sets its four extra emitters going (+0x20F0) always - MOST, "full" (s2,
    # a race's flag, 1).
    ("mod.particles_more", [(0x00261EB8, 0x24040001, 4)]),
    ("mod.particles_most", [(0x00261E6C, 0x24120001, 4)]),
    # No pause menu after five idle minutes.
    ("always", [(0x003954E8, 0x00000000, 4),
                # EA Trax in splitscreen: the music's update each frame
                # (0x003F8C30) stops the music (0x003FC470) with two players or
                # more (0x0064D3A0) - with three or more now (slti 2 -> 3).
                (0x003F8C54, 0x28410003, 4)]),
]

# The mods' own code, at the address their calls name.
CODE = [
    # mod.horn: the lights on (4) while the horn is held, off (0) otherwise.
    (0x000B40C0, [0x8E440CC0, 0x824313FC, 0x30630008, 0x10600002,
                  0x0000182D, 0x24630004, 0x03E00008, 0xA0830058]),
]


def containing(functions, address):
    for f in functions:
        if f["addr"] <= address < f["end"]:
            return f
        for start, end in f.get("chunks", ()):
            if start <= address < end:
                return f
    raise SystemExit("no function holds 0x%08X" % address)


def patched(image: bytes, base: int, words) -> bytes:
    data = bytearray(image)
    for address, value, size in words:
        data[address - base:address - base + size] = value.to_bytes(size, "little")
    return bytes(data)


def renamed(text: str, address: int, suffix: str) -> str:
    name = "F_%08X" % address
    head = "void %s(lifted::Context& c)" % name
    if head not in text:
        raise SystemExit("unexpected lifted text for %s" % name)
    return text.replace(head, "void %s_%s(lifted::Context& c)" % (name, suffix), 1)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", default=os.path.normpath(os.path.join(HERE, "..", "..")))
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    game = game_config.load(args.game)
    database = game.load_database()
    elf = game.load_elf()
    section = elf.exec_sections[0]
    image, base = elf.section_data(section), section.addr
    lift.use_sections(_other_sections(elf, section))
    lift.use_function_starts([f["addr"] for f in database["functions"]] + [a for a, _ in CODE])
    functions = [f for f in database["functions"] if f["end"] > f["addr"]]

    keys = [key for key, _ in MODS]
    # Each changed function: the mods changing it, in MODS' order.
    touched = {}
    for index, (_key, words) in enumerate(MODS):
        for address, _value, _size in words:
            f = containing(functions, address)
            touched.setdefault(f["addr"], (f, []))
            if index not in touched[f["addr"]][1]:
                touched[f["addr"]][1].append(index)

    out = ["/* The game's functions as PC SETTINGS' MODS change them: generated by",
           " * games/burnout3/port/tools/lift_mods.py from the player's executable at",
           " * build time (the game's code - never committed). Lifted, not verified. */",
           "",
           '#include "ps2rt/lifted.h"',
           '#include "ps2host/host.h"',
           "",
           "using namespace generated;",
           "using namespace lifted;",
           "",
           "bool ModOn(int mod);     // port/mods.cpp",
           ""]
    for address, words in CODE:
        code = b"".join(struct.pack("<I", w) for w in words)
        out.append(lift.lift_function(code, address, address, address + len(code)))
    dispatchers = []
    for address in sorted(touched):
        f, mods = touched[address]
        out.append("void F_%08X(lifted::Context& c);" % address)
        variants = ["F_%08X" % address]
        for mask in range(1, 1 << len(mods)):
            words = [w for bit, m in enumerate(mods) if mask >> bit & 1 for w in MODS[m][1]
                     if containing(functions, w[0]) is f]
            text = lift.lift_function(patched(image, base, words), base, address, f["end"],
                                      f.get("chunks", ()))
            out.append(renamed(text, address, "m%d" % mask))
            variants.append("F_%08X_m%d" % (address, mask))
        test = " | ".join("(ModOn(%d) ? %du : 0u)" % (m, 1 << bit) for bit, m in enumerate(mods))
        out += ["void Mods_%08X(lifted::Context& c) {" % address,
                "    static ps2host::GuestFunction const kVariants[] = {%s};" % ", ".join(variants),
                "    kVariants[%s](c);" % test,
                "}",
                ""]
        dispatchers.append((address, ", ".join(keys[m] for m in mods)))
    out.append("extern const char* const kModKeys[] = {%s};" % ", ".join('"%s"' % k for k in keys))
    out.append("extern const int kModCount = %d;" % len(keys))
    out.append("void RegisterModFunctions() {")
    for address, words in CODE:
        out.append('    ps2host::Override(0x%08Xu, F_%08X, "a mod\'s own code");' % (address, address))
    for address, which in dispatchers:
        out.append('    ps2host::Override(0x%08Xu, Mods_%08X, "MODS: %s");' % (address, address, which))
    out.append("}")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(out) + "\n")
    print("lift_mods: %d functions, %d variants" % (len(touched),
          sum((1 << len(m)) - 1 for _f, m in touched.values())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
