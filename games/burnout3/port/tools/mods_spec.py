"""Burnout 3's mods for PC SETTINGS' MODS: the spec engine/tools/lift_mods.py lifts.

A mod is a PCSX2 patch (Nehalem's, github.com/Nahelam/PS2-Game-Mods, Burnout 3
Takedown/Various and Burnout 3 Takedown/Crash Breaker Expansion, SLUS-21050 -
his work, with his credit: README.md's Credits): words written over the
game's code, and code of its own written to free memory. Each function a mod
changes is lifted again with the mod's words written in, once for each
combination of the mods that change it, and a mod's own code is lifted from
its words (engine/tools/lift_mods.py, at build time from the player's
executable; port/CMakeLists.txt). port/mods.cpp chooses among them as the
settings say.
"""

NAME = "games/burnout3/port/tools/mods_spec.py"

# A word of the EE kernel's memory (below 0x8000: lw from $zero reaches it),
# which the port's kernel leaves free: TRAFFIC's spacing (mods.cpp's
# kTrafficSpacing).
TRAFFIC_SPACING = 0x00000100

# The mods, in the order their words go in where two change the same word (a
# later one's wins): a setting's key, and its words - (address, value, bytes).
# A key of "always" is no setting: the port always has it.
MODS = [
    # The speedometer in km/h: the scale from miles (gp-0x7ED8) to kilometres
    # (gp-0x7D10), and the unit's string (0x7D8 MPH, 0x7D7 KPH).
    ("mod.kph", [(0x001A3BA0, 0x82F0, 2), (0x001A3D78, 0x07D7, 2)]),
    # The horn's volume (0 in the game) 1.0: Circle (and R3) honks - always
    # (no row: CONTROLS' HORN honks).
    ("always", [(0x0028CDD0, 0x3C023F80, 4), (0x0028CDE8, 0xAFA2005C, 4),
                  # The car's lights (+0x58 of its +0xCC0) on while the horn is held
                  # (+0x13FC bit 3): a call to the mod's code (0x000B40C0) in place
                  # of the store that turns them off.
                  (0x001C4934, 0x0C02D030, 4)]),
    # (Mirror mode, the patch's 0x0019EFA4 sb s2 - its caller's s2, whatever that
    # holds - is port/mods.cpp's: a 1 written after the function.)
    # Crash Breaker in every mode: Nehalem's Crash Breaker Expansion (Burnout 3
    # Takedown/Crash Breaker Expansion, SLUS-21050) - his earlier "in all
    # modes" patch taken further: the AI's cars and the other players caught
    # by the blast too, a race car Crash Breakered counted as an Aftertouch
    # Takedown, the Crash Breaker prompt while crashing (splitscreen's too).
    # Its test for crash mode gone (0x001BB9B8) and calls to its code
    # (CRASH_BREAKER_CODE below) in the race's crash, HUD and takedown code.
    # One of its words is port/mods.cpp's: the pointer it writes to data
    # (0x004DDE34 -> 0x000B3E20).
    ("mod.crash_breaker", [
                           (0x001BB9B8, 0x00000000, 4), (0x001BEA14, 0x0C02CE5C, 4), (0x001B2C90, 0x0260202D, 4), (0x001B2C94, 0x0C02CE84, 4),
                           (0x001B2C98, 0x0200282D, 4), (0x001B2D38, 0x0C02CEB4, 4), (0x001B2D50, 0x0C02CEE6, 4), (0x001B2D54, 0x0260202D, 4),
                           (0x001B2D58, 0x00000000, 4), (0x001B2D88, 0x0C02CEF2, 4), (0x001B2D8C, 0x0260202D, 4), (0x001B2D90, 0x00000000, 4),
                           (0x001B3714, 0x0260202D, 4), (0x001B3718, 0x0C02CEFE, 4), (0x001B371C, 0x0200282D, 4), (0x001B3724, 0x0040902D, 4),
                           (0x001B3728, 0x00000000, 4), (0x001B39D0, 0x0C02CF3E, 4), (0x001A1410, 0x0C02CF4A, 4), (0x003A34B8, 0x0C02CFB4, 4),
    ]),
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
    # TRAFFIC's density (where bryan's Traffic Density sections found it):
    # the traffic's spacing, 60.0 (lui 0x4270) in three places - the cars
    # on the road as a race starts (0x00266DA0, a0), the next car's time in
    # the race (0x002678B8, v1), the first cars' times (0x002682B0, v1) -
    # loaded instead from a word of the kernel's memory, free in the port
    # (TRAFFIC_SPACING; mods.cpp writes 60 / the density there).
    ("mod.traffic_density", [(0x00266DA0, 0x8C040000 | TRAFFIC_SPACING, 4),
                             (0x002678B8, 0x8C030000 | TRAFFIC_SPACING, 4),
                             (0x002682B0, 0x8C030000 | TRAFFIC_SPACING, 4)]),
    # No pause menu after five idle minutes.
    ("always", [(0x003954E8, 0x00000000, 4),
                # EA Trax in splitscreen: the music's update each frame
                # (0x003F8C30) stops the music (0x003FC470) with two players or
                # more (0x0064D3A0) - with three or more now (slti 2 -> 3).
                (0x003F8C54, 0x28410003, 4)]),
]

# mod.crash_breaker's own code (the Crash Breaker Expansion's, Nehalem's), from
# 0x000B3970: a function at each address its calls and its pointer name.
CRASH_BREAKER_BASE = 0x000B3970
CRASH_BREAKER_CODE = [
    0x27BDFFB0, 0x7FB10010, 0x00808825, 0x3C04004F, 0x7FB30030, 0x2484E040, 0x7FB20020, 0x7FB00000,
    0x00C09025, 0xFFBF0040, 0x00A08025, 0x0C04C790, 0x00E09825, 0x14400009, 0x00000000, 0x3C020065,
    0x24422660, 0x8C42AD1C, 0x28420002, 0x14400003, 0x00000000, 0x24020002, 0xA2021355, 0x02603825,
    0x02403025, 0x02002825, 0x0C06CAE8, 0x02202025, 0x92021355, 0x30420002, 0x10400002, 0x00000000,
    0xA2001355, 0xDFBF0040, 0x7BB30030, 0x7BB20020, 0x7BB10010, 0x7BB00000, 0x03E00008, 0x27BD0050,
    0x90821355, 0x24030002, 0x30460006, 0x10C30006, 0x00000000, 0x3C020052, 0x00101840, 0x03E00008,
    0x00701821, 0x00000000, 0x3C030052, 0x3C060013, 0x24632660, 0x00C33021, 0x8CC6AD1C, 0x00A6302A,
    0x10C00017, 0x00000000, 0x3C020004, 0x8C8413F4, 0x3442AB4A, 0x00A22821, 0x00052880, 0x00A32821,
    0x8CA20000, 0x1044000A, 0x00000000, 0x904218EA, 0x14400007, 0x00000000, 0x27FF0034, 0x8CA20000,
    0x2442FF90, 0x03E00008, 0x0040282D, 0x00000000, 0x27FF00A4, 0x03E00008, 0x00000000, 0x00000000,
    0x34420004, 0xA0821355, 0x0000802D, 0x3C020052, 0x00101840, 0x03E00008, 0x00701821, 0x00000000,
    0x90C21355, 0x24030002, 0x30420006, 0x10430004, 0x00000000, 0x0806E0BC, 0x00000000, 0x00000000,
    0x3C050004, 0x27BDFFD0, 0x34A2AB4A, 0x7FB00000, 0x02028021, 0x3C020052, 0x24422660, 0x00108080,
    0x02028021, 0x7FB10010, 0xFFBF0020, 0x3C040056, 0x8E030000, 0x00C08825, 0x906919AC, 0x2484E948,
    0x00091840, 0x00004025, 0x00691821, 0x00003825, 0x00031900, 0x90C60218, 0x00431021, 0x00A22821,
    0x0C06BD14, 0x8CA5C98C, 0x8E020000, 0x904419AC, 0x8E2213F4, 0x24830566, 0xC44010CC, 0x00031880,
    0x00431821, 0xDFBF0020, 0xE4600000, 0x00441021, 0x24030001, 0x7BB10010, 0x7BB00000, 0xA04315B0,
    0x03E00008, 0x27BD0030, 0x90821355, 0x30420002, 0x14400007, 0x00000000, 0x3C020064, 0x24422660,
    0x8C422B1C, 0x03E00008, 0x0002102A, 0x00000000, 0x03E00008, 0x24020001, 0x90821355, 0x24030002,
    0x30420012, 0x10430006, 0x00000000, 0x3C020064, 0x24422660, 0x03E00008, 0x8C422B1C, 0x00000000,
    0x03E00008, 0x24021337, 0x90831355, 0x24020002, 0x30660012, 0x10C2000A, 0x00000000, 0x3C030004,
    0x3C020052, 0x34638A84, 0x24422660, 0x00A32821, 0x00052880, 0x00451021, 0x03E00008, 0x8C420000,
    0x3C020052, 0x3C060013, 0x24422660, 0x00463021, 0x8CC6AD1C, 0x00A6302A, 0x10C00015, 0x00000000,
    0x3C060004, 0x8C8413F4, 0x34C3AB4A, 0x00A32821, 0x00052880, 0x00452821, 0x8CA30000, 0x1064001C,
    0x00000000, 0x906418EA, 0x10800019, 0x00000000, 0x906419AC, 0x00041840, 0x00641821, 0x00031900,
    0x00431021, 0x00C23021, 0x03E00008, 0x8CC2C98C, 0x34630010, 0xA0831355, 0x3C030012, 0x00431821,
    0x8C632B1C, 0x1860000E, 0x00000000, 0x0000802D, 0x3C030004, 0x00002825, 0x34638A84, 0x00A32821,
    0x00052880, 0x00451021, 0x03E00008, 0x8C420000, 0x27FF0370, 0x03E00008, 0x00001025, 0x00000000,
    0x1000FFFC, 0x27FFF680, 0x0260102D, 0x90421355, 0x24030002, 0x30420012, 0x10430003, 0x00000000,
    0x080730EC, 0x00000000, 0x27FF00B8, 0x080730EC, 0x00002825, 0x00000000, 0x27BDFFC0, 0xFFBF0030,
    0x7FB00000, 0x00808025, 0x7FB20020, 0x0C068268, 0x7FB10010, 0x3C04004F, 0x0C04C790, 0x2484E040,
    0x10400007, 0x00000000, 0xDFBF0030, 0x7BB20020, 0x7BB10010, 0x7BB00000, 0x03E00008, 0x27BD0040,
    0x3C110067, 0x24070002, 0x2624AFE0, 0x24060003, 0x00002825, 0x0C0E8E24, 0x2631AFE0, 0x44806800,
    0x24120001, 0x02202025, 0x0C0E8C94, 0xAE320044, 0x24030002, 0x8E020170, 0x1043000D, 0x00000000,
    0x1452FFE9, 0x00000000, 0x8E240004, 0x8E26002C, 0x24840014, 0x86250010, 0xDFBF0030, 0x7BB20020,
    0x7BB10010, 0x7BB00000, 0x08075D58, 0x27BD0040, 0x3C100067, 0x24070002, 0x2604C0A0, 0x24060003,
    0x0C0E8E24, 0x24050001, 0x2604C0A0, 0x44806800, 0xAC920044, 0xDFBF0030, 0x7BB20020, 0x7BB10010,
    0x7BB00000, 0x080E8C94, 0x27BD0040, 0x00000000, 0x27BDFFE0, 0xFFBF0010, 0x7FB00000, 0x0C0541F4,
    0x00808025, 0x3C04004F, 0x0C04C790, 0x2484E040, 0x1440000F, 0x00000000, 0x8E0427C0, 0x24034590,
    0x00832818, 0x3C020052, 0x24422660, 0x00A21821, 0x3C020006, 0x00431021, 0x24030001, 0x10800008,
    0xA043DA16, 0x24020001, 0x1082000D, 0x00000000, 0xDFBF0010, 0x7BB00000, 0x03E00008, 0x27BD0020,
    0x3C040067, 0xDFBF0010, 0x7BB00000, 0x24050001, 0x2484AFE0, 0x08067D08, 0x27BD0020, 0x00000000,
    0x3C040067, 0xDFBF0010, 0x7BB00000, 0x24050001, 0x2484C0A0, 0x08067D08, 0x27BD0020, 0x00000000,
    0x3C04004F, 0x27BDFFF0, 0xFFBF0008, 0x0C04C790, 0x2484E040, 0x10400006, 0x00000000, 0x8244001C,
    0x8E420004, 0xDFBF0008, 0x03E00008, 0x27BD0010, 0xDFBF0008, 0x27FF03AC, 0xFFBF0008, 0x00002825,
    0xDFBF0008, 0x27BD0010, 0x08067D08, 0x0240202D,
]
CRASH_BREAKER_ENTRIES = [0x000B3970, 0x000B3A10, 0x000B3AD0, 0x000B3B98, 0x000B3BC8, 0x000B3BF8, 0x000B3CF8, 0x000B3D28, 0x000B3E20, 0x000B3ED0]


def _crash_breaker_functions():
    ends = CRASH_BREAKER_ENTRIES[1:] + [CRASH_BREAKER_BASE + 4 * len(CRASH_BREAKER_CODE)]
    for start, end in zip(CRASH_BREAKER_ENTRIES, ends):
        first = (start - CRASH_BREAKER_BASE) // 4
        yield start, CRASH_BREAKER_CODE[first:first + (end - start) // 4]


# The mods' own code, at the address their calls name.
CODE = [
    # The horn's (always): the lights on (4) while the horn is held, off (0) otherwise.
    (0x000B40C0, [0x8E440CC0, 0x824313FC, 0x30630008, 0x10600002,
                  0x0000182D, 0x24630004, 0x03E00008, 0xA0830058]),
] + list(_crash_breaker_functions())
