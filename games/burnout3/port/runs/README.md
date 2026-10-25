# Scripted runs

Each `.args` file is a set of port options that drives the game to a known
point. With `--virtual-time` the run is the same on any machine, under any
load: the same presses land on the same frames and the pictures come out
byte-identical, so a run is a regression test and a way to get somewhere in
the game without playing.

```sh
burnout3.exe <SLUS_210.50> --disc <iso> $(cat games/burnout3/port/runs/car_select.args) \
    --frames out/ --frames-every 200
```

| file | reaches | frames |
| --- | --- | --- |
| `car_select.args` | no memory card -> start anyway; the EA, Criterion and RenderWare movies, the intro skipped (Start); title (Start); new profile (BURNOUT); don't save, autosave off; main menu -> World Tour -> USA -> Silver Lake -> Race -> the car select ("SELECT A COMPACT"), its 3D garage and cars | the logos ~1,500-2,200, the title ~3,750, the main menu at ~6,000, the car select at ~8,600 |
| `race_training.args` | the same, then a car chosen: RACE TRAINING, its tutorial movie ("RULES OF THE ROAD", played in a window; Start skips it - the runs after skip it) | the tutorial from ~9,500 |
| `race.args` | race_training's, the movie skipped (Start at 10,900), then the first race (Silver Lake) with Cross held from the start: traffic, a wall scraped, sparks, glare, a takedown | the race from ~11,660 to 13,410; the 3D the frame-rate work is measured on |
| `car_select_wide.args`, `race_wide.args` | car_select's and race's in a 16:9 picture (`--setting aspect=16:9`): the 3D wider at the camera, the HUD and the frontend's 2D in their 4:3 shape at their anchors (widescreen.cpp, the engine's ps2host/hud.h) | as car_select's and race's (GPU only) |
| `race_modern.args` | `race.args` as a player has it: the modern graphics (no `graphics=original`), 120 pictures a second from frame 11,500 (`--setting high_fps_from`: the presses land as at 60), drawn at 2x - the paths PGO is trained on too (game.json's `pgo_runs`; with this PC's texture pack, `pgo_env`) | the race from ~11,660 to 13,410 (GPU only) |
| `race_keys.args`, `race_bound.args` | `race.args` driven from the keyboard: X held (the keyboard's default ACCELERATE), and Space held with ACCELERATE bound to it in the settings (`--setting bind.keyboard.racing.cross=SPACE`) - the bindings (the engine's bindings.h, controls.cpp) read and applied; the two draw the same | the race from ~11,660 to 13,410 (GPU only) |
| `memcard_save.args` | a fresh memory card; the logos, the intro skipped, title, new profile, saved ("Save successful!"); the card's files are checked too | the save at ~4,500 |
| `race_mods.args` | `race.args` with PC SETTINGS' MODS on (`--setting`): KM/H, MIRROR MODE, HEADLIGHTS, no TRAFFIC, HORN (Circle held 18,200-18,400), CRASH BREAKER (R2 held from 18,700) - the mods' lifted functions (port/tools/mods_spec.py) | the race mirrored, km/h, from ~17,650 (GPU only) |
| `mods_garage.args` | car_select's start to the main menu, then PC SETTINGS -> DEVELOPER MENU -> Unlock All (back to PC SETTINGS), the main menu -> SINGLE EVENT -> RACE -> the garage, Right through the series: HEAVYWEIGHTS (always on), then the EXTRA SERIES (extra_series.cpp: the Tuk Tuk) | the developer menu ~7,300; the garage from ~9,500, HEAVYWEIGHTS after SPECIAL SERIES from ~10,040, EXTRA SERIES from ~10,100 (GPU only) |
| `splitscreen.args` | car_select's start to the main menu, then MULTIPLAYER -> SPLITSCREEN -> PLAYERS: two scripted controllers (`--press pad1:start`, `pad2:start`) seated, the game's MULTIPLAYER (its 2-player modes open); one Down and Up, one Right and Left (no press taken twice, though every device drives both ports in the menus), RACE, both cars chosen with player 1's presses, then the race split in two, player 2's controller alone accelerating | PLAYERS ~6,900; MULTIPLAYER ~7,300; the cars ~8,000-8,800; the race from ~9,300 (GPU only) |
| `crash.args` | `mods_garage`'s way to the main menu with Unlock All, then SINGLE EVENT -> CRASH -> junction 1 (TRAILER TRASH) -> the first car; the CRASH TRAINING movie skipped (Start at 10,450); the junction driven into at full throttle, the pile-up, CRASHBREAKER (R2), the score's tally and CRASH RESULTS | the junction list ~9,000; the crash from ~12,160; the results ~14,760 (GPU only) |
| `road_rage.args` | the same to SINGLE EVENT, then ROAD RAGE (region and track Random: a USA mountain course), the first car, the RACE TRAINING movie skipped (Start at 10,450), then full throttle: takedowns, a wreck into the wall (aftertouch), tailgating, FRENZY | the options ~9,000; Road Rage from ~10,760 (GPU only) |
| `eliminator.args` | the same to SINGLE EVENT, then ELIMINATOR (its fourth row; single_event.cpp: the World Tour's Eliminator rules on a race), region and track Random, the first car, the RACE TRAINING movie skipped, then full throttle: 5 laps, last at lap 1's end - ELIMINATED! and the results | the list ~8,700; the race from ~12,000; ELIMINATED! ~19,500 (GPU only) |
| `splitscreen_vertical.args` | `splitscreen.args` with MODS' SPLITSCREEN VERTICAL (`--setting mod.vertical_split=on`): the race's views side by side | the race from ~9,300 (GPU only) |

The frame numbers are vertical blanks counted by the host, so they hold only
with `--virtual-time`; in real time the presses drift with the machine's speed.
The CD clock (sceCdReadClock) follows the virtual clock too, from a fixed date:
the game seeds its EA Trax playlist from it.

The same run under two renderers draws the same pictures, which is how the GPU
renderer is checked against the software GS:

```sh
burnout3.exe ... $(cat car_select.args) --frames soft/ --frames-every 200
burnout3.exe ... $(cat car_select.args) --gpu --frames gpu/ --frames-every 200
python engine/tools/framediff.py soft/ gpu/ --diff-dir diff/
```

`--gs-dump-frame N` adds GS local memory after frame N, to compare buffer by
buffer with `engine/tools/gsmem.py`.

## Regression checks

`engine/tools/regress.py` runs one of these and compares its pictures with
the CRCs recorded beside it (`<run>.gpu.crc`, `<run>.soft.crc`; the pictures
themselves are never committed):

```sh
python engine/tools/regress.py --game games/burnout3 car_select --gpu          # about a minute
python engine/tools/regress.py --game games/burnout3 race_training --gpu --every 250
python engine/tools/regress.py --game games/burnout3 car_select                # the software GS
```

A change meant to alter the pictures - or the game's timing, which moves
them - records them anew with `--update`; the commit says why. A change that
should not, and does, is a regression: a speed-up to the GPU bridge that lost
an image's own drawing showed as the race's crash replay going wrong, found
by `framediff.py` against the software run and `--gs-dump-frame` from both
builds.

The GPU runs come out the same every time. The software GS's longer runs do
not quite: `race` differs on two or three frames from 17,800 on, and
`race_training` now and then at 9,000, changing from one run to the next -
on engine v0.2.0 as on v0.4.0. A difference there alone is not a regression;
one in a GPU run, or at other frames, is.

## Memory card

`memcard_save.args` boots with an empty memory card (`{memcard}`: regress.py
gives each run a fresh directory), creates a profile from the front end and
saves it; the CRCs cover the pictures and the three files the game wrote
(`memcard/BASLUS-21050/...`). The other runs have no card, as they were
recorded.
