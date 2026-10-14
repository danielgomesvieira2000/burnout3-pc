Burnout 3: Takedown - PC port, version {version}
=================================================

An unofficial, fan-made native Windows port of Burnout 3: Takedown (PS2,
NTSC-U). The game's PS2 program was translated to C++ and compiled for the
PC; it runs on your computer directly, not in an emulator.

This download contains NO game content. You need your own copy of the
game: an ISO disc image you made from your own Burnout 3: Takedown disc.
Nothing of the game - no textures, sounds, music, movies or data - is
included, and none is downloaded: everything comes from your disc image.

Not affiliated with, endorsed by or sponsored by Electronic Arts or
Criterion Games. "Burnout" is a trademark of Electronic Arts Inc.

This is an ALPHA: expect bugs.

Made with AI: this port is built by Claude Code (Anthropic's Claude),
directed, playtested and reviewed by its author.


What you need
-------------

* Windows 10 or 11, 64-bit
* A processor with AVX2 (Intel Haswell / AMD Ryzen or newer)
* A graphics card with a Vulkan 1.3 driver (NVIDIA GTX 900 series, AMD
  RX 400 series, Intel UHD 600 series or newer, with current drivers)
* Your own disc image of Burnout 3: Takedown, the North American (NTSC-U)
  release: SLUS-21050. Other regions are not supported yet.


Getting started
---------------

1. Unzip this folder somewhere you can write to (your Documents or Games
   folder - not Program Files): the game keeps its saves and settings
   beside burnout3.exe.
2. Run burnout3.exe.
3. The first time, it asks for your disc image: choose your .iso. It
   checks that it is the right disc and remembers it.

The game plays in a window or fullscreen (Alt+Enter switches). In
fullscreen on a 144 Hz screen it switches to 120 Hz, so that the game's 60
frames a second show evenly.


Controls
--------

A game controller (Xbox, PlayStation, most others SDL knows) works as a
DualShock 2. Rumble is supported.

Modern controls (PC SETTINGS, CONTROLS) - while racing only; the menus and
the pause screen keep Cross and Triangle:
  R2   accelerate (how far you pull)    Cross     boost, Impact Time
  L2   brake, reverse (how far)         Square    look back
  R1   boost                            Triangle  change camera
  L1   next EA Trax song                Circle    as before
With TRIGGER EFFECTS on, a DualSense's triggers push back while you race.

Keyboard:
  Arrow keys        D-pad            Enter       Start
  X                 Cross            Backspace   Select
  C                 Circle           Q / E       L1 / R1
  Z                 Square           1 / 3       L2 / R2
  V                 Triangle

Mouse, in the menus: point at an item to highlight it, click to choose it,
right-click to go back, wheel to move up and down. The buttons along the
bottom of the screen can be clicked too, and so can the keys of the name
keyboard. On the car select, click the chosen car (on the left) to go on
with it, or the one above or below it to turn to that one.

The button icons follow what you play with: the controller's buttons, the
keys while you use the keyboard, the mouse's buttons once you use the mouse.

Other keys:
  Alt+Enter   fullscreen or a window
  Tab         a texture pack off and on (see "Texture packs")
  F10         the HUD editor in widescreen: PgUp/PgDn choose an element,
              Home left, Insert centre, End right, Delete stretch; P pauses
  F12         a capture of the frame, for a bug report (captures\ folder)


PC settings
-----------

In the main menu, PC SETTINGS (where ONLINE used to be - EA's servers are
long gone) has three pages (Triangle goes back):

  VIDEO      fullscreen, vertical sync, frame pacing, the internal
             resolution (original, twice as sharp, or as sharp as your
             screen - up to 4x), the aspect ratio (4:3, 16:9 or your
             window's), and anti-aliasing: off, FXAA (edges smoothed,
             almost free), or SSAA 2X / 4X (drawn 2 or 4 times as sharp and
             averaged down - the cleanest, and the heaviest on the GPU;
             with the resolution at DISPLAY, SSAA 4X can be too much)
  GRAPHICS   the speed blur
  CONTROLS   modern controls (triggers to accelerate and brake, face
             buttons for the rest - see "Controls") and, with them, a
             DualSense's trigger effects

QUIT GAME is in the main menu too.


Texture packs
-------------

Texture packs made for PCSX2 work here too: put the pack's files in the
"texture pack" folder beside burnout3.exe (folders inside it are fine) and
start the game. Tab turns the pack off and on while you play. A texture is
replaced the first time it is drawn, a moment after it appears.


Files beside burnout3.exe
-------------------------

  pc_settings.ini   your settings, and where your disc image is
  memcard\          your memory card: the game's saves
  texture pack\     a PCSX2 texture pack, if you have one
  burnout3.log      what the last run did (useful in a bug report)

To choose a different disc image, delete the "disc=" line from
pc_settings.ini.


Known issues
------------

* Only the NTSC-U release (SLUS-21050) runs.
* Online play is not available.
* A heavy moment in a race can still show a picture a frame late.
* The World Tour map takes the mouse's wheel, right button and
  bottom-bar buttons, but not pointing yet.
* The DualSense's trigger effects are new and lightly tested; tell us
  how they feel (USB and Bluetooth).


Licences
--------

The port uses SDL 3, volk, glslang, the Vulkan headers, xxHash, bcdec and
stb_image: their licences are in THIRD-PARTY-NOTICES.txt.
