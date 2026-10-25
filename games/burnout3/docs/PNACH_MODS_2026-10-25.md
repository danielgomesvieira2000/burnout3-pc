# The pnach mods of 2026-10-25 (reverted)

The night of 2026-10-25 brought the player's PCSX2 mods (`b3 mod pnachs/` at
the repository's root, never committed) into PC SETTINGS > MODS, all at once,
through one generic pipeline. A plain launch with some of them saved ON then
stopped the game at boot (below), so the whole of it was reverted on master the
next morning, keeping that night's graphics, the IOP interrupt fix and the
music watchdog. The mods are to come back one at a time, each the right way
for that mod.

The work itself is kept, complete and building, on the branch
`pnach-mods-2026-10-25` in both repositories (game 34c6292, engine 0ce0e87).
The general lessons are in [FINDINGS.md](FINDINGS.md), "PCSX2 mods with code
of their own".

## What was built

**One pipeline for every pnach** (engine `tools/lift_mods.py`, game
`port/tools/mods_spec.py`'s `PNACHES`: a setting's key, a file, a section):

1. Each section's words are read from the player's `.pnach` at build time.
2. The functions the words change are found, and from them the mod's own
   code: a hook's `j` into a cave and the cave's `j` back (the cave lifted as a
   chunk of the hooked function), `jal` targets, pointers the mod writes,
   addresses built with `lui`/`addiu` that read as functions, `sltiu` +
   `lui`/`addiu` jump tables. A delay slot that a jump lands on later is
   followed again.
3. Every function is lifted once per combination of the mods that change it
   (up to six mods sharing one; identical results deduplicated), spread over 8
   shards; a chooser picks the variant by `ModOn`.
4. The mods' words also go into memory (`kModWrites`): all of a mod's as it
   is turned on (the game's own words saved and put back once it is off), the
   pnach's every-frame words once a vertical blank, as PCSX2 does.
5. Callers whose stack frames a mod reads (Car Colour checks its caller's
   saved `$ra`) are translated plainly while it is on.

**Supporting engine changes** (all reverted with it): `SetUnmappedIgnored`
(reads of no memory return 0 and writes are dropped while a pnach mod is on,
as on PCSX2); `Disc::AddFiles` / `Disc::OnOpen` (a folder's files laid over the
disc image, its directory sectors patched, for the Traffic Series' cars); the
lifter treating a register jump to the function's own return address as a
return. `--setting-at KEY=VALUE@FRAME` and the scripted sticks (`--press
rs_left@...`) were kept: they are general test tools.

**Port side** (reverted): a `Modded<addr, F>` wrapper so the port's own
overrides of a function a mod also changes call the mod's variant, with
`RegisterMods` moved first; mod rows hidden when their pnach was not there at
build time; mid-race changes to a pnach mod deferred to the next load.

## The mods

Words are the pnach section's patch lines.

| Row | Section (author) | Words | Verified in a scripted run | Known problems |
| --- | --- | --- | --- | --- |
| SPEEDOMETER STYLE: BETA HUD | Beta HUD (Remco/SuperType1) | 1,207 | yes | |
| SPEEDOMETER STYLE: RPM METER | RPM Meter + Speed Unit (bryan) | 857 | shares memory with the Beta HUD, one at a time | |
| DEMO CRASHES | Demo Crashes (Remco/SuperType1) | 39,437 | crash camera, dents | reads a pointer table of the demo's build (garbage, unmapped); switched off mid-crash froze the race; ON at boot stopped the game (below) |
| ORBIT CAMERA | Right-stick orbit camera (Remco/SuperType1) | 6,579 | the stick turns it | its wall check pulls the camera into the car on some streets |
| CAMERA ROLL | Camera Roll in Corners (bryan) | 144 | | |
| END OF RACE CAMERA | April beta set pieces (bryan) | 6,688 | Eliminator to its end | |
| TRAFFIC CHECK | Traffic Check, Revenge style (bryan) | 1,328 + 1,385 | yes | part of its code sits in Projected Shadows' runtime scratch (0x001D9800-0x001D9C40): lifted but never written there |
| TRAFFIC DENSITY | x1.5 / x2 / x3 (bryan) | 6 each | x3 | |
| TRANSMISSION CHOICE | Transmission Toggle (bryan) | 241 | the popup | |
| CAR COLOURS | Player Car Color (bryan) | 10,136 | after 8+ R2 presses (the list starts with three reds) | needs its callers' frames (plain translation) |
| ELIMINATOR | Eliminator, Single Event (bryan) | 476 | in the list and a race | |
| GARAGE STATS | Garage Stats (bryan) | 955 | yes | |
| RACER BLINKERS | Racer Blinkers, April beta (bryan) | 553 | no | |
| BOOST BAR SHAKE | Boost Bar Shake, April beta (bryan) | 25 | no | |
| TAKEDOWN PITCH | Takedown Pitch by Count (bryan) | 32 | no | |
| ROAD RAGE PARTS | detach parts in road rage (bryan) | 1 | yes | |
| GRAPHICS: CAR SHADOW SHAPE | Projected Shadows (bryan) | 2,195 | | its scratch overlaps Traffic Check's code |
| GRAPHICS: XBOX REFLECTIONS | Xbox-style reflections v2, garage v2, draw distance (bryan) | 2,165 + 75 + 4 | | a one-frame boost-bar glitch at race start; its code words must still be written (something reads them as data: skipping them turned the reflections white) |
| TRAFFIC SERIES | Traffic Series (bryan; Heavyweights by escape209) | 103 + its vlist.bin's code | garage and a race in the Tuk Tuk | needs its 9.3 MB `game_files` at run time (reworked game assets: never in the repository); its vlist.bin carries code loaded at 0x005186A0 |

All of them at once were raced at 120 fps, in splitscreen and with MODERN
graphics.

## Why it was reverted

- **The game stopped at boot** with Demo Crashes saved ON ("a write at
  0x02000000, outside guest memory", in Demo Crashes' code called from
  0x0019B380). The choosers picked the mod's code from the settings alone,
  but its words (its data) are only written at the first display frame step,
  and the boot runs before that. Scripted runs never showed it: they skip the
  player's `pc_settings.ini` (`--virtual-time`) and apply `--setting` after
  registration. The fix (game 34c6292, on the branch: `ModOn` waits for a
  pnach mod's words) came after the player had already lost trust in the
  build.
- **A freeze at launch** ("not responding" for about 15 s, Windows closing
  it) was seen twice by the player before any new mod was on, and once by me;
  not reproduced afterwards and not explained.
- **Too much at once**: about 75,000 words of other people's code, behind one
  generic pipeline, with workarounds that hold for the whole game while any
  mod is on (unmapped accesses ignored) and three mods never seen working.

## Bringing them back, one at a time

For each mod:

1. Read its section and say what it changes: data only, a few instructions,
   or code of its own (and how much).
2. Choose the smallest way in: a data write from the frame step, a word in
   `mods_spec.py`'s `MODS` like Nehalem's, its code lifted, or rewritten in
   C++ (ours to publish) where it is small.
3. Test it the way a player uses it: a plain launch with it saved ON, turned
   on and off in the menu and mid-race, and the recorded runs with it OFF.
4. Commit it alone.

Only the general machinery a mod needs comes back with it. Before any of the
mods' own words go into the public repository, their authors' permission is
needed: none of the files has a licence.
