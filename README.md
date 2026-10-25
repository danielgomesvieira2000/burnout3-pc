# Burnout 3: Takedown - PC port

**Burnout 3: Takedown, native on Windows.** The PS2 game's program translated
to C++ and compiled for the PC - not emulated: 60 frames a second or your
screen's own rate, up to 4x the PS2's resolution, modern graphics, widescreen
with the HUD kept in place, PC settings in the game's own menu, a mouse in the
menus, controllers with rumble and DualSense adaptive triggers.

> **Unofficial and fan-made.** Not affiliated with, endorsed or sponsored by
> Electronic Arts or Criterion Games. You need your own copy of the game: a
> disc image made from your own Burnout 3: Takedown disc (NTSC-U, SLUS-21050).
> No game content is in this repository or in the release.

## Playing it

Download `Burnout3-PC-<version>-windows-x64.zip` from
[Releases](../../releases), unzip it somewhere you can write to, run
`burnout3.exe` and choose your disc image. It needs Windows 10 or 11 (64-bit)
and a Vulkan 1.3 graphics driver - nothing to install. (A processor without
AVX2 - Intel's Pentium and Celeron before 2021 - gets the build made for it,
`burnout3-sse4.exe`, started by `burnout3.exe` itself.)
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
- **Modern graphics** (PC SETTINGS > GRAPHICS, a PRESET row on top:
  ORIGINAL or MODERN, MODERN by default, then each one on its own): a
  lighting grade, ambient occlusion, a modern glow and the camera's own
  motion blur (each MODERN HIGH or LOW; LOW in the MODERN preset), the sun's
  shadows, lacquered reflections on the cars' paint, depth of field in the
  takedown cameras, soft smoke and fire,
  contrast-adaptive sharpening - drawn inside the game's own frame, the HUD
  on top.
- **Texture filtering of your choice**: ORIGINAL, MODERN, NEAREST,
  BILINEAR, TRILINEAR or ANISOTROPIC 4X/8X/16X - on the game's textures and
  a texture pack's alike.
- **Widescreen**: 4:3, 16:9 or the window's own shape, with the HUD kept in
  place - and a HUD editor (**F10**) to anchor each HUD element left, right,
  centred or stretched.
- **Rebindable controls**: every key and controller button, with racing and
  menu controls set apart (Cross can choose in the menus without
  accelerating); a CLASSIC or MODERN (triggers) racing layout.
- **Button icons that follow you**: the game's button prompts show the keys,
  mouse buttons or controller buttons you use - as you bound them, in the
  menus and in the race - with PlayStation or Xbox marks (automatic by
  controller), all drawn by the port.
- **PC settings** in the game's own main menu (where ONLINE used to be), in
  pages: VIDEO (fullscreen, vertical sync, the frame rate, resolution, the aspect ratio,
  anti-aliasing: FXAA, SMAA or SSAA 2x), GRAPHICS (a preset, lighting,
  ambient occlusion, bloom, motion blur, shadows, reflections, cinematic
  cameras, smoke and fire, texture filtering, sharpening, LOD, more
  particles),
  CONTROLS (the keyboard's and the controller's bindings, racing and menus;
  button prompts; DualSense trigger effects), MODS (Nehalem's game patches:
  km/h, mirror mode, traffic off or 0.5x to 3x, car parts shed in Road Rage, Crash Breaker in every mode (the Crash Breaker Expansion),
  headlights, heavyweights, a vertical splitscreen) and the game's hidden
  DEVELOPER MENU. Saved in `pc_settings.ini`; QUIT GAME in the main menu.
- **Two players on one PC**: MULTIPLAYER > SPLITSCREEN - each player presses
  Start on the controller (or Enter on the keyboard) they play with, the
  first player 1, the next player 2; in the race each drives their own car,
  in the menus every device works as before. Both halves drawn at the
  internal resolution, with the race's music.
- **High frame rates**: PC SETTINGS > VIDEO > FRAME RATE - 60, 120 or
  DISPLAY (your screen's own rate, 144, 165, up to 240) in races, crashes,
  splitscreen and the menus. The game still runs at its own 60 steps a
  second - nothing plays faster - and the pictures between are drawn
  between them: the cars, the traffic, the cameras, the garage's car - a
  camera's cuts and a wrecked car put back on the road shown as they happen,
  not slid between. Where the PC cannot keep up, a race steps down to 120 or
  60 by itself.
- **Smooth frames**: at 60, 120 Hz in fullscreen on a 144 Hz screen so that
  60 frames a second show evenly (frame pacing off by default:
  `frame_pacing=on` in `pc_settings.ini` turns it on).
- **Controls**: any controller SDL knows (Xbox, PlayStation, ...) as a
  DualShock 2, with **rumble**; the keyboard; the **mouse in the menus**
  (point, click, right-click to go back, wheel).
- **DualSense adaptive triggers** from the car's physics: the accelerator
  free through its play, then a spring that firms with the engine's pull
  and a step at full throttle, light when the rear lets go; the brake firming
  with the speed and giving way past the grip's limit; a hit felt in both.
  TRIGGER EFFECTS: OFF, LIGHT or ON.
- **Texture packs**: PCSX2 texture packs for the game work as they are
  (below); **Tab** turns one off and on.
- **Saves** on a memory card folder beside the executable; the disc image
  chosen once and remembered; Alt+Enter for fullscreen.
- **Bug reports**: a log of every run, and F12 captures of a frame; an FPS
  counter on **F1**.

## LAN play

MULTIPLAYER > **LAN**: the game's own online mode, over the local
network or a VPN that acts like one (ZeroTier, Radmin VPN, Hamachi). There is
no server to run - the first player in serves the others, inside the game.

- **Choosing the network**: the game's list of network configurations shows
  AUTO (every network) and each of the PC's networks by its address, its
  adapter's name below. A player on two networks (the real LAN and a VPN)
  takes the one the others are on. The list is made again each time LAN is
  taken.
- **Finding the lobby**: the game asks on the chosen network (a UDP
  broadcast, port 21801); the lobby answers with its address on that
  network. None answering, this game serves. A lobby served on a chosen
  network answers only that network's players (on AUTO, every network's);
  it closes when its player leaves it for the menus (not for a race).
- **Accessing network**: the game's UPnP search (four tries, ~2 s each, for
  a router to open a port on) is skipped - a LAN has none - so the lobby
  comes at once.
- **The firewall**: TCP 21800 (redirector) and 10901 (lobby), UDP 21801
  (discovery) and 3658 (the race, player to player). Windows often counts a
  VPN's adapter as a public network.
- **Not yet**: players with no network in common (one only on the real LAN,
  one only on a VPN) - the race goes from player to player, so it would need
  a relay; and VPNs without broadcast (Tailscale, WireGuard), which need
  `PS2HOST_INET_HOST` set by hand.

| Variable | What it does |
| --- | --- |
| `B3_LAN=0` | LAN greyed in MULTIPLAYER |
| `B3_LAN_IP=<address>` | this PC's address for the LAN (the list's choice is left alone) - also two instances on one PC |
| `PS2HOST_INET_HOST=<address>` | the lobby is at that address: no looking for it |
| `B3_LAN_ACCOUNTS=1` | SELECT EA ACCOUNT shown (by default it goes by, logged in as the PC's player) |
| `B3_LAN_KEEP_NETWORK=1` | LAN taken again goes straight to the lobby on the network chosen before (by default the list of networks comes back) |
| `B3_LAN_DEBUG=1` | the online flow, the lobby's messages and libnet's traffic, step by step, in the log |
| `B3_LAN_DUMP=1` | each race datagram, with its first bytes |

The code: [lan.cpp](games/burnout3/port/lan.cpp) (the game's side: the
network list, DNAS, the account), [lan_lobby.cpp](games/burnout3/port/lan_lobby.cpp)
(the lobby and its discovery), [lan_libnet.cpp](games/burnout3/port/lan_libnet.cpp)
(the race's sockets).

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
appears (its file is read in the background). A pack's textures follow
TEXTURE FILTERING too, filtered by the graphics card itself; a texture saved
without mipmap levels (most packs' are) gets them made as it is read, so it
does not shimmer in the distance. Texture packs are made by their authors
from the game's textures: none is included or linked here.

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
- The heaviest moments at high frame rates (a Crash Breaker's explosion at
  144 a second): the GPU bridge's work per primitive, the VU1 code.
- Better performance on slower machines.
- The ambience skipping in Impact Time's slow motion, now and then (#20).
- SPLITSCREEN side by side: parts of the race's HUD in the wrong place.
- The World Tour map: pointing with the mouse.
- The other releases of the game (PAL, NTSC-J).
- Steering wheel support, with force feedback.
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
and Ninja) and Python 3.10+ (Pillow for the art scripts); with
[LLVM](https://github.com/llvm/llvm-project/releases) installed, port.py
builds with its clang-cl - the release's compiler, its code faster
(`PS2_COMPILER=msvc` for MSVC's own). See the engine's
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

## Credits

### Contributors

- **PaPe** ([@nynjardin](https://github.com/nynjardin)) - **LAN play**: the
  game's online mode on the PC's network, its lobby served by the game itself
  and found on the network, the race between players
  ([lan.cpp](games/burnout3/port/lan.cpp),
  [lan_lobby.cpp](games/burnout3/port/lan_lobby.cpp),
  [lan_libnet.cpp](games/burnout3/port/lan_libnet.cpp); game pull request
  [#4](https://github.com/danielgomesvieira2000/burnout3-pc/pull/4)), and in
  the engine Sony's inet on the PC's sockets, the IOP's fixes for EA's
  network module, the **GPU renderer on a thread of its own** and a fix to
  the sampler (engine pull request
  [#1](https://github.com/danielgomesvieira2000/ps2-port-engine/pull/1)).

### Game patches

The port's 60 FPS menus and crashes, its DEVELOPER MENU and the PC SETTINGS
MODS page come from PCSX2 patches made for the game, brought into the port
(the port runs the game's code translated, so each is lifted again with the
patch in: [lift_mods.py](engine/tools/lift_mods.py), [mods_spec.py](games/burnout3/port/tools/mods_spec.py),
[mods.cpp](games/burnout3/port/mods.cpp),
[sixty_fps.cpp](games/burnout3/port/sixty_fps.cpp),
[dev_menu.cpp](games/burnout3/port/dev_menu.cpp)). From
[Nehalem's PS2-Game-Mods](https://github.com/Nahelam/PS2-Game-Mods)
(Burnout 3: Takedown, SLUS-21050):

| In the port | Patch | Author |
| --- | --- | --- |
| 60 FPS menus, movies and crashes (always on) | Graphics: 60 FPS Menus, 60 FPS Crashes | Nehalem ([@Nahelam](https://github.com/Nahelam)) |
| PC SETTINGS: DEVELOPER MENU | Development Menu (dev_menu) | Nehalem |
| MODS: SPEEDOMETER (KM/H) | mph2kph | Nehalem |
| The horn (always on: CONTROLS' HORN), the headlights while it sounds | horn, headlights_hold | Nehalem |
| MODS: MIRROR MODE | partial_mirror_mode | Nehalem |
| MODS: TRAFFIC (off) | disable_traffic | Nehalem |
| MODS: TRAFFIC (0.5X to 3X) | where the traffic's spacing is, from his Traffic Density sections (the port's own change: the spacing read from memory, any density) | bryan |
| MODS: ROAD RAGE PARTS | detach parts in road rage (one instruction: the modes' skip gone) | bryan |
| MODS: CRASH BREAKER (all modes): rivals and other players caught in the blast, a rival blown up an Aftertouch Takedown, the prompt while crashing (splitscreen too) | [Crash Breaker Expansion](https://github.com/Nahelam/PS2-Game-Mods/tree/main/Burnout%203%20Takedown/Crash%20Breaker%20Expansion) - its hooks and its own code, lifted (it replaces his earlier crashbreaker_in_all_modes) | Nehalem |
| MODS: HEADLIGHTS (always on) | perma_headlights | Nehalem |
| GRAPHICS: PARTICLES (more, most) | Render extra particles while driving | Nehalem |
| MODS: HEAVYWEIGHTS (all modes) | heavyweights_always_available | [escape209](https://github.com/escape209) (the original SLUS patch; ported to the other releases by Nehalem) |
| MODS: SPLITSCREEN (vertical) | vertical_split_screen | Nehalem |
| No pause after five idle minutes (always on) | disable_idle_check | Nehalem |

The patches are their authors' work, brought into the port with their
credit; they remain theirs. The port carries their words (the changed
instructions and, for the Crash Breaker Expansion, its own code) in
mods_spec.py, nothing of the game's.

### Third-party software

[SDL 3](https://github.com/libsdl-org/SDL), [volk](https://github.com/zeux/volk),
[glslang](https://github.com/KhronosGroup/glslang), the
[Vulkan headers](https://github.com/KhronosGroup/Vulkan-Headers),
[xxHash](https://github.com/Cyan4973/xxHash),
[bcdec](https://github.com/iOrange/bcdec),
[stb_image](https://github.com/nothings/stb) and
[SMAA](https://github.com/iryoku/smaa) (Jorge Jimenez, Jose I. Echevarria,
Belen Masia, Fernando Navarro, Diego Gutierrez), each under its own licence.
[PCSX2](https://pcsx2.net) was the reference the port was checked against.

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
is built with SDL 3, volk, glslang, the Vulkan headers, xxHash, bcdec,
stb_image and SMAA, under their own licences (the release's
THIRD-PARTY-NOTICES.txt; see Credits).
