# Burnout 3: Takedown - PC port

**Burnout 3: Takedown, native on Windows.** The PS2 game's program translated
to C++ and compiled for the PC - not emulated: 60 frames a second at up to 4x
the PS2's resolution, widescreen with the HUD kept in place, PC settings in the
game's own menu, a mouse in the menus, controllers with rumble.

> **Unofficial and fan-made.** Not affiliated with, endorsed or sponsored by
> Electronic Arts or Criterion Games. You need your own copy of the game: a
> disc image made from your own Burnout 3: Takedown disc (NTSC-U, SLUS-21050).
> No game content is in this repository or in the release.

## Playing it

Download `Burnout3-PC-<version>-windows-x64.zip` from
[Releases](../../releases), unzip it somewhere you can write to, run
`burnout3.exe` and choose your disc image. It needs Windows 10 or 11 (64-bit),
a processor with AVX2 and a Vulkan 1.3 graphics driver - nothing to install.
The README in the zip has the controls, the PC settings and the known issues
([games/burnout3/port/release/README.txt](games/burnout3/port/release/README.txt)).

This is an **alpha**: please report problems in Issues, with the
`burnout3.log` written beside the executable. For a picture that looks
wrong, press **F12** at that moment and attach the folder it writes to
`captures/` beside the executable (it replays the frame exactly).

## What it does

- **The whole game, natively**: the PS2 program translated to C++ and
  compiled for x64 - menus, races, crash mode, the career, saves - with its
  sound and music, reading everything from your own disc image.
- **A GPU renderer (Vulkan)**: the PS2's graphics drawn on your graphics
  card, at the PS2's resolution, twice it, or as sharp as your screen (up to
  4x).
- **Widescreen**: 4:3, 16:9 or the window's own shape, with the HUD kept in
  place - and a HUD editor (**F10**) to anchor each HUD element left, right,
  centred or stretched.
- **Button icons for the keyboard and mouse**: the game's button prompts turn
  into keys (or mouse buttons) as soon as those are used, and back to the
  controller's when it is.
- **PC settings** in the game's own main menu (where ONLINE used to be), in
  pages: VIDEO (fullscreen, vertical sync, frame pacing, resolution, the
  aspect ratio, anti-aliasing: FXAA or SSAA 2x/4x), GRAPHICS (the speed blur),
  CONTROLS (modern controls: R2/L2 accelerate and brake, analogue, face
  buttons for boost and the rest, while racing only; DualSense trigger
  effects). Saved in `pc_settings.ini`; QUIT GAME in the main menu.
- **Smooth frames**: frame pacing, and 120 Hz in fullscreen on a 144 Hz
  screen so that 60 frames a second show evenly.
- **Controls**: any controller SDL knows (Xbox, PlayStation, ...) as a
  DualShock 2, with **rumble**; the keyboard; the **mouse in the menus**
  (point, click, right-click to go back, wheel).
- **Texture packs**: PCSX2 texture packs for the game work as they are
  (below); **Tab** turns one off and on.
- **Saves** on a memory card folder beside the executable; the disc image
  chosen once and remembered; Alt+Enter for fullscreen.
- **Bug reports**: a log of every run, and F12 captures of a frame.

## Texture packs

A texture pack made for PCSX2 (for Burnout 3: Takedown, SLUS-21050) works
here too:

1. Put the pack's files - the `.dds` or `.png` files PCSX2 keeps in
   `textures/SLUS-21050/replacements/` - in the **`texture pack`** folder
   beside `burnout3.exe`. Folders inside it are fine; only the files' names
   count (they name the texture each replaces, as PCSX2 names them).
2. Start the game. Its log says how many textures it found.
3. Press **Tab** to turn the pack off and on while you play (the choice is
   kept in `pc_settings.ini`).

A texture is replaced the first time it is drawn, a moment after it first
appears (its file is read in the background). Texture packs are made by
their authors from the game's textures: none is included or linked here.

## AI usage

**This port was made with AI.** It is built and maintained by
[Claude Code](https://claude.com/claude-code) (Anthropic's Claude), directed,
playtested and reviewed by the repository's author. The engine's tools
translate the game's program automatically; the rest - the runtime, the GPU
renderer, the PC features, the fixes, the tests and these documents - was
written by Claude, working from the game itself, PCSX2 as a reference and
its own test runs. Commits carry a `Co-Authored-By: Claude` line. The
workflow it follows is in the engine's skills (`engine/.claude/skills`).

## To do

- More playtesting: whole careers, every event type, every track.
- Higher frame-rate settings (beyond the game's 60 frames a second).
- More controls: button remapping, DualSense haptics, steering wheels with
  force feedback.
- Graphical mods and options (beyond texture packs).
- Better performance on slower machines.
- Linux, macOS and Android ports.

## Building it

The port is made with [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine),
the game-independent toolchain and runtime, which is this repository's
`engine/` submodule (pinned to a tag):

```sh
git clone --recursive https://github.com/danielgomesvieira2000/burnout3-pc
cd burnout3-pc
# your disc image: "Burnout 3 - Takedown.iso" beside games/ (game.json's "iso")
python engine/tools/isotool.py extract "Burnout 3 - Takedown.iso" SLUS_210.50 -o games/burnout3/extracted/
python engine/tools/analyze.py --game games/burnout3
python engine/tools/derive.py  --game games/burnout3 --emit --csv
python engine/tools/lift.py    --game games/burnout3 --structure --emit --csv
python engine/tools/port.py    --game games/burnout3 build
python games/burnout3/port/tools/package.py        # the release zip
```

For full speed, record the game's VU microprograms once and build again - they
are compiled ahead of time from that list (without it they run interpreted,
VU1 about 1.5x slower): run `port/runs/car_select.args` and
`port/runs/race_training.args` with `--vu-interpret --vu-record
games/burnout3/build/vu_programs.txt` (the engine's docs/PORTING.md, "VU
microprograms compiled ahead of time").

The game's code in C++ is generated on your machine from your disc image and
never committed. Building needs Visual Studio 2022 with C++ (its MSVC, CMake
and Ninja) and Python 3.10+ (Pillow for the art scripts). See the engine's
[docs/PORTING.md](https://github.com/danielgomesvieira2000/ps2-port-engine/blob/master/docs/PORTING.md)
for every option of the port, and its skills (`engine/.claude/skills`) for the
workflow Claude Code follows here.

## What is where

```
engine/                     ps2-port-engine (submodule)
games/burnout3/
  game.json                 the game: its disc, executable, title, version
  port/                     the port: overrides.cpp (the game's own functions,
                            the host's way: PC SETTINGS, quit, movies, loading),
                            widescreen.cpp, mouse.cpp, the icon and pointer
                            (original art, drawn by tools/), release/, runs/
                            (the scripted test runs and their pictures' CRCs)
  config/                   the function table, names, syscalls
  docs/                     FINDINGS (what the game taught), PROGRESS, plans
  tests/, tools/            the game's own tests and tools
```

## Legal

This repository contains **no game assets, no disc images or BIOS files, and
none of the game's code.** The game's code in C++ - lifted, derived or
reconstructed - is generated on your own machine from your own disc image and
never committed (`.gitignore`). What the repository holds is original: the
port's code, the PC features and their art, documentation of how the game
works (addresses, function names, findings) and checksums of test pictures.

The Windows release contains the game's program translated to C++ and
compiled, with no game assets: it reads everything it plays from the player's
own disc image, and refuses an image that is not the game's disc.

Reverse engineering for interoperability and preservation is protected in many
jurisdictions. This project follows the conventions of the wider game
preservation community: it redistributes no game assets and does not try to
substitute for the commercial product. "Burnout" is a trademark of Electronic
Arts Inc.

The project's own code is under the MIT licence ([LICENSE](LICENSE)). The port
is built with SDL 3, volk, glslang, the Vulkan headers, xxHash, bcdec and
stb_image, under their own licences (the release's THIRD-PARTY-NOTICES.txt).
