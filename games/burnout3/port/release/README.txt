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
* A 64-bit processor from 2008 or later. burnout3.exe is built for
  processors with AVX2 (Intel Haswell / AMD Ryzen or newer); on one
  without it (Intel Pentium and Celeron before 2021) it starts
  burnout3-sse4.exe, beside it, by itself
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
frames a second show evenly - unless FRAME RATE is DISPLAY: then it keeps
the screen's own rate (144).


Controls
--------

A game controller (Xbox, PlayStation, most others SDL knows) works as a
DualShock 2. Rumble is supported.

Every key and button can be changed (PC SETTINGS, CONTROLS), the keyboard's
and the controller's each, and each in two sets: RACING and MENUS (the
menus, the pause screen, message boxes). So Cross can choose in the menus
and still not accelerate, or W can accelerate while the arrows move through
menus. Choose an action and press the new key or button; Escape, a right
click or five seconds leave it as it was. A key already used for something
else in the same set swaps over to it. RESET TO DEFAULTS on each page puts
it back. The choices are kept in pc_settings.ini.

The controller's racing LAYOUT: CLASSIC (the game's own) or MODERN - while
racing only:
  R2   accelerate (how far you pull)    Cross     boost, Impact Time
  L2   brake, reverse (how far)         Square    look back
  L1   next EA Trax song                Triangle  change camera
Accelerate or brake on a trigger is analogue, whatever the layout. With
TRIGGER EFFECTS on, a DualSense's triggers push back while you race.

The keyboard's default keys (racing and menus alike):
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
keys while you use the keyboard, the mouse's buttons once you use the mouse -
and always show the keys and buttons you chose, in the menus and in the race
("PRESS E KEY TO BOOST"). BUTTON PROMPTS chooses the controller's: AUTO (an
Xbox controller's letters for an Xbox controller, PlayStation marks for the
rest), PLAYSTATION or XBOX.

Other keys:
  F1          an FPS counter on and off (pictures shown a second)
  F9          the engine's PC settings panel over the game (also L3 + R3)
  Alt+Enter   fullscreen or a window
  Tab         a texture pack off and on (see "Texture packs")
  F10         the HUD editor in widescreen: PgUp/PgDn choose an element,
              Home left, Insert centre, End right, Delete stretch; P pauses
  F12         a capture of the frame, for a bug report (captures\ folder)


PC settings
-----------

In the main menu, PC SETTINGS (where ONLINE used to be - EA's servers are
long gone) has four pages and the game's hidden menu (Triangle goes back):

  VIDEO      fullscreen, vertical sync, the frame rate (60; 120 for a
             120 Hz or faster screen; or DISPLAY, your screen's own rate -
             144, 165, up to 240 - in races, splitscreen too, and the
             menus: the game still runs at its own 60 steps a second, the
             pictures between drawn between them - the cars, the camera,
             the garage's car; the menus' flat pictures and movies move at
             60; where the PC cannot keep up, a race steps down to 120 or
             60 by itself), the internal resolution
             (original, twice as sharp, or as sharp as your screen - up to
             4x), the aspect ratio (4:3, 16:9 or your window's), and
             anti-aliasing: off, FXAA (edges smoothed, almost free), SMAA
             (edges smoothed more carefully, still light), or SSAA 2X
             (drawn twice as sharp and averaged down - the cleanest, and
             the heaviest on the GPU)
  GRAPHICS   the game's own effects, each as the PS2 drew it, changed or
             off: BLOOM (the glow around lights: original, high, off or
             low), MOTION BLUR (boosting's and the camera's), SHADOWS (softer, or
             none), LOD (the cars' detailed models farther away -
             heavier on the processor) and PARTICLES (more smoke and
             sparks while driving: Nehalem's patch)
  CONTROLS   KEYBOARD (RACING), KEYBOARD (MENUS), CONTROLLER (RACING),
             CONTROLLER (MENUS): every key and button (see "Controls");
             BUTTON PROMPTS (auto, PlayStation or Xbox) and a DualSense's
             TRIGGER EFFECTS
  MODS       Nehalem's patches for the game (see Credits): SPEEDOMETER in
             km/h, a HORN, MIRROR MODE, no TRAFFIC, CRASH BREAKER in every
             mode (Nehalem's Crash Breaker Expansion: the rivals and the
             other players caught in the blast too, a rival's car blown up
             an Aftertouch Takedown, the prompt while you crash),
             HEADLIGHTS always on, the HEAVYWEIGHTS in every mode, and the
             SPLITSCREEN one above the other or side by side
  DEVELOPER MENU   the game's own hidden menu: unlock all, lock all, the
             awards (what it unlocks is kept when the game saves)

QUIT GAME is in the main menu too.


Texture packs
-------------

Texture packs made for PCSX2 work here too: put the pack's files in the
"texture pack" folder beside burnout3.exe (folders inside it are fine) and
start the game. Tab turns the pack off and on while you play. A texture is
replaced the first time it is drawn, a moment after it appears.


Custom songs
------------

Your own MP3s play with the game's EA TRAX songs: put them in the
"custom_songs" folder beside burnout3.exe. They show up in the EA TRAX menu
(DRIVER DETAILS, SETTINGS, EA GAMES TRAX) after the game's 44, with the same
ALL / RACE ONLY / MENU ONLY / OFF choice, and play in the rotation - random or
in order, their name on screen as each starts, L2 to skip one.

Name each file "Artist - Song Title.mp3":

    Fuzzeke - Without A Care.mp3      shown as  Fuzzeke / Without A Care
    My Song.mp3                       shown as  Unknown Artist / My Song

Names are cut to 36 characters (with "...") to fit the game's screens. Songs
added or removed while the game runs are picked up within a few seconds. Up
to 100 songs.


Two players on one PC
---------------------

MULTIPLAYER, then SPLITSCREEN. Each player presses Start on the controller
they play with (Enter on the keyboard, or the key you bound): the first one
in is player 1, the next player 2. Then CONTINUE, to the game's own
multiplayer races. While racing, each player's device drives their own car
only; in the menus every device - keyboard, mouse, controllers - works as
before. Back at the main menu the seats are free again.


LAN play
--------

Race each other on a local network, or over a VPN that acts like one
(ZeroTier, Radmin VPN, Hamachi): MULTIPLAYER, then LAN. No server to run:
the first player in serves the others.

1. Everyone takes MULTIPLAYER, then LAN.
2. The list of network configurations shows this PC's networks:
     AUTO               every network at once
     192.168.1.20       one network, by its address (the adapter's name
     ETHERNET           below it: Ethernet, Wi-Fi, ZeroTier, Radmin...)
   Take AUTO if you all share one network. On two networks at once (the
   real LAN and a VPN), take the one the others are on: everyone on a
   ZeroTier network takes that ZeroTier network's line.
3. The game looks for a lobby on that network; if none answers, yours is
   the lobby and the others join it.

The list is made again each time you take LAN: a VPN connected while the
game runs shows up the next time.

Windows' firewall: allow burnout3.exe when Windows asks, on private AND
public networks - Windows often counts a VPN's adapter as public. Or open
TCP 21800 and 10901, UDP 21801 and 3658. Check first that the players can
ping each other's address on the network they share.

Not yet possible:
* Players with no network in common - one only on the real LAN, another
  only on a VPN - cannot race each other, even through a player on both.
* VPNs that carry no broadcast (Tailscale, WireGuard): the lobby is not
  found by itself. Start the game from a command line with
  PS2HOST_INET_HOST set to the lobby's address (see below).

For the curious (environment variables, set before starting the game):
  B3_LAN=0              LAN greyed in MULTIPLAYER
  B3_LAN_IP=<address>   this PC's address for the LAN; the list's choice
                        is then left alone
  PS2HOST_INET_HOST=<address>   the lobby is there: no looking for it
  B3_LAN_DEBUG=1        the LAN's every step in burnout3.log, for a bug
                        report


Files beside burnout3.exe
-------------------------

  pc_settings.ini   your settings, and where your disc image is
  memcard\          your memory card: the game's saves
  texture pack\     a PCSX2 texture pack, if you have one
  custom_songs\     your own MP3s for EA TRAX (see "Custom songs")
  burnout3.log      what the last run did (useful in a bug report)

To choose a different disc image, delete the "disc=" line from
pc_settings.ini.


Known issues
------------

* Only the NTSC-U release (SLUS-21050) runs.
* Online play (EA's servers) is not available; LAN play is (see LAN play).
* SPLITSCREEN side by side (MODS: SPLITSCREEN VERTICAL): parts of the
  race's HUD are still drawn in the wrong place.
* A heavy moment in a race can still show a picture a frame late.
* The World Tour map takes the mouse's wheel, right button and
  bottom-bar buttons, but not pointing yet.
* The DualSense's trigger effects are new and lightly tested; tell us
  how they feel (USB and Bluetooth).


Credits
-------

LAN play, the GPU renderer's own thread: PaPe (github.com/nynjardin).

The 60 FPS menus and crashes, the DEVELOPER MENU, the no-idle-pause fix
and the MODS page come from PCSX2 patches for the game, from Nehalem's
PS2-Game-Mods (github.com/Nahelam/PS2-Game-Mods):
  Nehalem      60 FPS Menus, 60 FPS Crashes, Development Menu, mph2kph,
               horn, headlights_hold, partial_mirror_mode, disable_traffic,
               Crash Breaker Expansion, perma_headlights,
               vertical_split_screen, disable_idle_check
  escape209    heavyweights_always_available (the original SLUS patch;
               ported to the other releases by Nehalem)

SMAA: Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and
Diego Gutierrez (github.com/iryoku/smaa).


Licences
--------

The port uses SDL 3, volk, glslang, the Vulkan headers, xxHash, bcdec,
stb_image and SMAA: their licences are in THIRD-PARTY-NOTICES.txt.
