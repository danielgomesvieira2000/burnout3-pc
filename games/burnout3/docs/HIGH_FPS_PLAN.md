# Higher frame rates: a plan for interpolated frames

Status: **step 1 done (2026-10-20)**; written 2026-10-10 from what the port
showed of the game then.

## Step 1, done: two pictures a step (port/high_fps.cpp)

`frame_rate=120` in pc_settings.ini (or PS2HOST_FRAME_RATE=120; off by
default, no menu entry yet). While a one-player race runs - its world
stepping (RaceWorld calls) and fewer than 2 players (0x0064D3A0) - the
vertical blanks come twice as often (SetFastForward(2)) and the world
(0x0019A550) steps at every second picture. (+0x2DA65, the "half rate
asked", is no guide: it stays set after a race's first crash.)

- **Cost, measured** (bench.sh, 1x, all cores, RTX): 119.9 new pictures a
  second, presents every 8.34 ms (jitter 0.1-0.2 ms), 0 late; VIF1's worker
  41-49% busy, the GS thread 35-44%, the EE 31-37% of a core, the process
  1.87 cores. The VU1 blocker the plan feared is gone (phase C2): two
  pictures a step fit.
- **Pictures in pairs**: in virtual time (the blanks at 60, the gating as at
  120) pictures 2k and 2k+1 show the same world, and differ only in what
  moves with the picture: the radial blur (it blends with the last picture:
  at 120 its trail is half as long) and the HUD's flashing.
- The display: 144 Hz on the author's laptop - 120 pictures under 144 Hz
  vsync are not evenly paced. Interpolation at any fraction of a step
  (below) is what lets the rate follow the display's (144) later.

## Step 2, done: the pictures between steps (port/high_fps.cpp)

After each step the race's transforms are taken down by shape - every
RwMatrix (three orthonormal rows, a finite position) between 0x00500000 and
0x00700000, where engine/tools/find_transforms.py found all 80 that move in
the scripted race: the six racers, their wheels (car-local, about +-0.8,
+-1.2), two arrays of copies a car each (strides 0x180 and 0x50) - and, for
the picture drawn then, each that moved is placed half way back towards the
step before (positions averaged, rotations the rows' means made orthonormal
again; one that jumped more than 50 units a step is a cut and left alone);
before the next picture and the next step it is put back. The pictures show
the race at N - 1/2, N, N + 1/2, N + 1: evenly, half a step (8 ms) late.
The camera is made in the draw from the cars, and follows.

- **Checked** in virtual time, the mean difference between successive
  pictures: without the placing 0.3, 18.4, 0.4, 13.4, 0.2 (pairs, then a
  step); with it 14.5, 13.7, 10.3, 8.7, 8.6, 9.0, 9.4.
- **Real time** (bench.sh, 1x): all cores 0 late, 99.9% of frames within
  8.75 ms, the process 1.93 cores (the EE ~44%: the take-down's scan of 2 MB
  a step is ~8 of it); 4 threads 3 late, 99.9% within 12.5 ms, 2.17 cores.

**Next:** FRAME RATE in PC SETTINGS (60 / 120); the radial blur and the HUD
at a step's pace; the display's own rate (144 Hz: the placing at any
fraction, not only a half); the take-down cheaper (the addresses kept from
step to step); then the mechanism into the engine for every game.

## For every game (the engine)

The mechanism is not Burnout 3's: a game's world stepped at its own rate
and the pictures drawn between, from its moving objects' transforms blended.
What moves to the engine (ps2host), once step 2 works here:

- the rate (the blanks' multiple, or the display's refresh), the setting
  (`frame_rate`), and the stepping cadence: an accumulator that says, at
  each picture, whether the world steps and at what fraction between its
  last two steps the picture is;
- snapshots of registered transforms (RenderWare's RwMatrix - right, up, at,
  pos: most PS2 games of the time - and plain float vectors), their blend
  (positions lerped, rotations slerped and made orthonormal again), placed
  before an in-between picture and put back after it;
- cut detection (a transform that jumps further than a step's travel: shown
  as it is, not blended);
- a tool to find a game's moving transforms (RAM snapshots a step apart:
  what changes by small, smooth amounts, in RwMatrix shape).

What a game supplies: its world-update function (where the step is gated),
when high frame rates are allowed (Burnout 3: a one-player race), and where
its transforms are (or a function listing them each step).

## The goal

Show races at 120 Hz (or any multiple of 60, later any refresh rate) without
changing how the game plays. Burnout 3 simulates its race at a fixed 60 steps a
second (the race clock moves 1/60 s a frame), so the game logic stays at 60.
Extra frames are drawn *between* simulation steps, from the cars and camera
placed between their last two states - the same idea as PC games' "render
interpolation".

Raising the simulation rate itself (running the game logic at 120) is not the
plan: its physics, timers and AI assume 1/60 s steps, and speeding them up
changes how it plays.

## What the port already shows (measured, not assumed)

1. **A race can be redrawn without being simulated.** The HUD editor's pause
   (c4d3569) skips the world update, 0x0019A550, while the draw, 0x00134A20,
   runs every frame: 300 paused frames came out identical pixel for pixel,
   and the race went on unchanged after. The draw doesn't move the game on -
   it can be called a second time in a frame.
2. **Where the movement happens.** The cars' positions are written in the
   world update (0x0019A550 > 0x00166160 > 0x00186850 > 0x00172E40; the
   player's car at 0x00539D90 in the scripted race). The camera's matrix
   (RenderWare's RwFrame at 0x01ED0C20) is rebuilt in the *draw*, from the cars
   (0x00134A20 > 0x002768F0 > 0x00276300 > 0x00227360 > ... > 0x001E1410). So
   if the cars are placed between two steps before a draw, the camera follows
   them by itself.
3. **The clock.** The race clock (0x0064E47C and copies per car) and the
   frame's timers advance in the world update, once a step.
4. **What a frame costs.** At 3x on an RTX 4070 Laptop a race frame takes the
   GPU ~38% of a 60 Hz frame and the VU1 worker (the vertex processing) 75-90%.
   A second frame per step doubles the VU1 and GS work: **the VU1 worker is the
   bottleneck** - it would need to do in 8.3 ms what takes it ~13 ms now.

## The approach

Each 60 Hz step, as now: simulate (0x0019A550), then draw. Between steps, for
each extra display frame at fraction t (0 < t < 1) of the way to the next step:

1. **Snapshot.** After each world update, record the state that moves: every
   car's matrix (position + orientation), the wheels', the traffic's, and
   anything else that is drawn from a per-frame transform (debris, smoke
   emitters' positions). Keep the last two snapshots.
2. **Place.** Before an extra draw, write each object's transform as the blend
   of its last two: positions lerped, orientations slerped (from the rotation
   part of the 4x4, renormalised). Set the race's clock and any draw-time timer
   to the blended time.
3. **Draw.** Run the draw (0x00134A20) - the camera rebuilds itself from the
   placed cars; the HUD is drawn as usual.
4. **Restore.** Put the transforms (and the clock) back as the simulation left
   them, so the next world update starts exactly where it would have.
5. **Present** the extra frame through the existing frame pacing (the display
   thread already shows frames at a due time: FrameDue / PresentImageAt).

Interpolation shows each step 1/60 s late (the frame between the last two
states). Latency goes up by up to one 60 Hz frame; a later version can
extrapolate instead, at the risk of overshooting in crashes.

## What has to be found first (unknowns)

* **The full list of moving transforms.** The cars are found; the rest -
  traffic, crash debris, particles, the sky and shadows - must be listed. The
  tool for it: the snapshot/compare method used tonight (two RAM dumps a frame
  apart, plus `--watch-write` on each candidate to find its writer). Anything
  missed shows as an object that jerks at 60 while the rest moves at 120.
* **State the draw changes.** The pause test shows the draw is repeatable *with
  the world frozen*; with the world interpolated, any draw-time state that
  depends on time (particle animation, the speed blur's trail, the glow's
  accumulation from the last frame) needs checking: a second draw may advance
  it twice. The radial blur in particular blends with the previous picture -
  drawn twice as often, it trails half as long.
* **Crashes and cuts.** A camera cut (crash replay, takedown cam) between two
  steps must not be blended across - detect a jump in the camera or a mode
  change and show the new state alone.
* **Game logic that reads the transforms during the draw** (e.g. lens flare
  visibility tests, the HUD's rival labels from 3D positions): reading the
  placed values is fine; writing them is not - check with write watches during
  an extra draw.

## The costs to solve before it's worth turning on

* **VU1 throughput (the blocker).** Two frames per step need roughly twice
  the VU1 work. Options, in order of promise:
  1. Run the two draws' VU1 work on two workers (the extra frame's command
     stream is independent of the next step's once captured) - needs the VIF1
     worker split into a pool.
  2. Speed up the compiled VU programs: program 26 alone is a third of the
     worker's busy time; it reads its STATUS flags, so the flag-free "fast"
     variant can't be used - flag liveness per block (not per program) would
     let most of it drop the flag work.
  3. Skip the extra frame when the worker is behind (adaptive: 120 where the
     machine keeps up, 60 where it doesn't).
* **GS/GPU.** The GPU has the headroom (38% at 3x); the GS thread on the CPU is
  ~75% busy and would need the same parallelism or trimming as VU1.

## Order of work

1. **Prototype at a fixed 2x (120 Hz) without interpolation**: draw every step
   twice with the same state. It measures the real cost (VU1, GS, GPU) and
   shows which draw-time effects run twice as fast. Nothing is blended yet.
2. **Snapshot/restore of the player's car and the camera only**: the extra
   frame shows the player's car and the camera between steps. Judge it in play;
   traffic still steps at 60 (a visible limit, fine for evaluating).
3. **All cars and traffic**, then debris and particles, found with the RAM-diff
   tooling.
4. **Effects that depend on frame count** (speed blur, glow trails): scale them
   by the frame time so they look the same at 120.
5. **Performance**: VU1 parallelism or flag liveness, until 120 holds on the
   target machine; adaptive fallback to 60.
6. **A PC SETTINGS entry** (FRAME RATE: 60 / 120 / DISPLAY) once it holds up.

Each step is checked the usual way: the scripted race's pictures at 60 must
stay exactly as recorded (the extra frames are the only new output), and the
real-time race must hold its rate (`--frame-times`).
