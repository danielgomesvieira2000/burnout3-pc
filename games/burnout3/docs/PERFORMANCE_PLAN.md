# Performance and frame pacing: the plan

Goal (2026-10-10): better than a PS2 emulator on the same machine, and **no
dropped frame in gameplay** - every one of the race's 60 pictures a second
shown on time.

## How it is measured

* **The profiler** - `PS2HOST_PROFILE=file` (`ps2host/timeline.h`): every
  thread's work (the EE, the VIF1 worker, the GS thread, the present thread)
  and every picture's way to the screen, as a timeline (Chrome's trace
  format: ui.perfetto.dev), and a summary at the end: the present intervals'
  percentiles, the frames dropped or late, and for each late one what was
  busy before it. `engine/tools/timeline.py` lists every thread's events
  around a moment (`at`) or the longest spans of each kind (`longest`).
* **The benchmark** - the race script (`port/runs/race.args`) under
  `--virtual-time`, exactly to the race, then `--real-time-from 18000`: the
  race played in real time, windowed, at a fixed 3x resolution (1920 x 1344,
  heavier than 1080p), sound on, the driving extended to frame 21400 and
  profiled from 18600 - 2,800 frames, 47 seconds of racing.
* **Dropped** means any of: a picture dropped before it was shown, a present
  interval past 1.5 frames (the last picture shown twice), a vertical blank
  the EE was too busy for (the game itself loses a step).

## Where it stands (the baseline, 2026-10-10)

The race at 3x on an RTX 4070 Laptop: 2,801 pictures in 2,801 blanks, none
dropped, none lost - but **3 shown late** (37.4, 29.5, 25.3 ms), and the
interval's 99th percentile 20.5 ms (jitter 1-2 ms around 16.7).

| thread | busy | what |
| --- | --- | --- |
| EE | ~38% | the game (translated code), the IOP, the SPU2 |
| VIF1 worker | 50-73% | VU1's vertex programs (compiled), VIF1 |
| GS thread | 40-72% | the GS model, the GPU bridge (draws, textures, memory) |
| GPU | 30-46% | drawing at 3x |

The late frames: the GS thread stopped 23 ms in "GPU and memory in step"
(the CPU waiting for the GPU to copy memory back) at the worst; VU1 ~33 ms
of work in the same moments (a crash's debris and cars on screen).

## Done so far

* **The CPU no longer waits for the GPU when a transfer writes part of a
  block the GPU drew last.** The race's CLUT uploads (8 x 3 and 16 x 4
  pixels, into blocks the narrow 64 x 512 targets' write-backs left the
  GPU's) brought each block back first - 228 times in the race, 10-14 ms
  each, the GPU's whole queue waited for. Now the transfer's pixels are
  patched into the GPU's memory once it is done (a compute shader, eight
  masked words a dispatch), the blocks staying the GPU's: none brought back.
* **The benchmark's pacing.** After `--real-time-from` the host's clock runs
  on from the virtual one; FrameDue compared it with the blanks' steady
  clock and paced nothing - most of the "late" frames measured first were
  that. FrameDue keeps to the steady clock now (play was not affected).

With both: the race at 3x, **2,800 pictures, none late**, interval median
16.69 ms, 99% 18.1, longest 21.9, jitter 0.45 ms; a frame's way from its
blank to the screen 1.5 ms median, 10.5 at most. Per frame, the GS thread
10.2 ms median (15.2 at the 99th percentile), VIF1's worker 10.5 (15.8):
the headroom that is left.

The player's screen (2560 x 1440 at 144 Hz): "display" resolution is 3x,
as measured; fullscreen goes to 120 Hz (EvenMode), so 60 pictures a second
each show for two refreshes. In a window the desktop's 144 Hz can't show
60 evenly - a window shows a picture for two refreshes, then three.

Then (8471128, 429353d, 409a126), each found as a picture shown late:

* the blanks' timer and the present thread woke late with every core busy
  (a blank 13 ms late): the highest priority, high-resolution waitable
  timers; Windows 11 dropping the timer resolution for a hidden window
  (a 200 us sleep took 15 ms): the process asks to keep it;
* the present thread waited for a swapchain image at the picture's time:
  acquired ahead now;
* FrameDue: a 6 ms floor, and two seconds' history (heavy moments come
  together);
* image memory sub-allocated from 64 MB blocks, the texture cache 4,096:
  a new stretch of track no longer goes to the kernel for each texture.

Now, at 3x: the race 0-1 frames late in 2,800 (99% 17.1 ms, 99.9% 18.7-19.2),
the menus 0 late in 4,400 (two runs). The one left: the race's frame ~20840,
the GS thread's own work there (texture uploads and their decoding, ~25 ms
in one frame).

## The plan, in order of what drops frames first

1. **No CPU wait for the GPU in a frame.** Find every download in the race
   (the profiler names them: a texture, a read, a write) and remove its
   cause: textures read from targets decoded on the GPU (they are, where
   the GPU holds the blocks - find the ones that are not), CPU reads of GPU
   memory answered later or not at all, the CLUT loads' syncs.
2. **Spikes of GPU bookkeeping.** New textures and images in a crash (first
   use: decode, upload, pipeline creation) - spread out, cached, or made
   ahead (pipelines at boot from a list, as the VU programs are compiled).
3. **VU1 headroom.** The worker is at 50-73% with spikes past a frame:
   flag liveness per block (not per program) so the exact mode's flag work
   drops where nothing reads it; the hottest programs' code (program 26 a
   third of the time); split the frame's VU1 work over two workers.
4. **The GS thread's headroom.** The GS model per primitive (the bridge's
   state compare, vertex conversion) - the sampler (`--sample-gs`) says
   where.
5. **Pacing.** The present thread wakes by sleep and yield: a waitable
   timer (high resolution) and the swapchain's present mode (mailbox or
   FIFO by the vsync setting) - the jitter's 1-2 ms down; frames presented
   at the vertical blank's due time, not when ready.
6. **Elsewhere in gameplay.** The same measure on the other modes - crash
   mode, road rage, the car select, the race's start (the loading screen's
   end) - and their spikes.
7. **Against PCSX2.** The same race in PCSX2 at the same resolution on the
   same machine: its frame rate and its frame times (its own on-screen
   statistics), beside the port's.

Each step: the benchmark before and after (the summary's late frames and
percentiles), the regressions (car_select, race_training, race) as
recorded.

## Low-power machines (2026-10-16/17)

Goal: smooth on low-end PCs - few cores, slow cores, weak GPUs.

**How it is measured.** The same race benchmark, at the PS2's own
resolution (what a weak machine runs), the process held to a few cores from
its start with `PS2HOST_CORES` (a count, or a mask: on the i7-13620H used,
logical cores 0-11 are six hyperthreaded pairs, so `PS2HOST_CORES=4` is two
physical cores with hyperthreading - a low-end i3 - and `0x5` two physical
cores without - a Celeron or Pentium Silver). A process held to fewer cores
only after it started is not the same: the GPU driver's thread pools were
sized for sixteen, and every result measured so was wrong. Optionally a
"burner" takes a share of each core at a higher priority (a slower CPU;
imprecise with hyperthreading). Per run: frames dropped, blanks lost, late
presents, the interval's percentiles, and the process's CPU (cores used).

**Where it stood.** Two physical cores: 163 frames dropped, 2,390 blanks
lost, the game stopping for up to 4.8 seconds at a time.

**What fixed it.**

| change | two cores | sixteen |
| --- | --- | --- |
| before | 163 dropped, 2,390 lost, longest 4,774 ms, 1.52 cores | 0 late |
| the vertical blank timer no longer spins on a timer interrupt it marked due and the EE has not taken yet (it ran at time-critical priority, holding a core the EE needed) | 0-2 dropped, 0-3 lost, 41-44 late, longest 67-113 ms, 1.40 cores | 0 late, 1.44 cores |
| spinning to a deadline and the game threads above normal only with 4+ logical cores (with 2: 1,118 dropped) | (included above) | unchanged |
| VU1 fast mode by default (exact pictures; the scripted race 102.8 -> 98.3 s, VU1 29.2% -> 25.6%) | | |
| SPU2: a voice heard nowhere skips its interpolation (WAV byte-identical; SPU2 5.5% -> 5.3%) | | |

Two physical cores with hyperthreading (`PS2HOST_CORES=4`): 0 dropped, 5
late in 2,800 (11 without the spin). With a burner taking about half of
every core as well: 0 dropped, 2-3 late.

What is left on two cores: in heavy moments (crashes, debris) VU1, the GS
thread and the EE together need more than two cores for a few frames - the
41 late presents. Per frame on the fast CPU, at the PS2's resolution: VIF1's
worker ~49% busy (89% of that VU1's compiled code, spread over many blocks),
the GS thread ~44% (25% drawing, ~25% Vulkan recording and the driver), the
EE ~37% (a third of it the SPU2). Measured and found not to matter: the GS
thread's spin before it sleeps (1.46 vs 1.44 cores), the speed blur (2-3%
of the GPU), the 1024 x 1024 depth texture decoded each frame (no change).

The GPU at the PS2's resolution: about 5.5 ms a frame on an RTX 4070 laptop
(draws 55%, swizzles 20%, decodes 15%): ~1,870 draws in ~216 render passes,
~410 barriers, ~200 dispatches a frame - each texture decode goes through a
staging buffer and a copy between full barriers. An integrated GPU will feel
that first; the next lever there is fewer passes and barriers (decodes
written straight into their images, batched).

### The integrated GPU (2026-10-17)

The i7-13620H's own Intel UHD, chosen with `PS2HOST_GPU=intel`, is the
low-end GPU measured. The race at the PS2's resolution:

| | dropped | lost | late | texture decodes' share of the GPU |
| --- | --- | --- | --- | --- |
| before | 290 | 613 | 1,108 | 55-65% |
| decodes into the image directly (no staging copy) | 293 | 532 | 1,098 | 55-65% |
| decode.comp's layout tables read from a buffer | 1 | 0 | 4 | 3-6% |
| the same, held to two physical cores (`PS2HOST_CORES=0x5`) | 0 | 0 | 2 | |

decode.comp indexed constant arrays (up to 256 entries) per texel; Intel's
driver copies such an array into each invocation's private memory, the
NVIDIA driver serves it from its constant cache - so it showed only on the
integrated GPU. swizzle.comp had already been rewritten to compute its
layouts (bit permutations); the decode had not. Pictures are the same
(regressions as recorded).

A low-end PC - two cores and an integrated GPU - now plays the race with
nothing dropped and two frames late in 2,800.

### Later the same night

* **A pipeline cache kept between runs** (gpu_pipeline_cache.bin and
  gpu_pipelines.txt beside pc_settings.ini): the race's 30 pipelines were
  built mid-game (0.4-9.9 ms each on the Intel UHD with its driver's own
  cache warm); a second run makes them at start in 3 ms and none while
  playing.
* **Debug switches read once** (getenv per texture decode and per frame):
  0.3% of the GS thread's samples.
* Measured and left alone: doubling every memory barrier does not change
  the integrated GPU's busy time (59-69% at the PS2's resolution) - its time
  is drawing, not barriers, so narrowing them would buy nothing; the VU1
  dispatch loop is a table lookup a block - VU1's time is the exact cycle
  model itself.
* The high-end setup (3x, all cores): 0 late in 2,800, 99.9% 17.6 ms (was
  18.7-19.2), 1.43 cores.

Where it stands (the race, real time): two physical cores and an integrated
GPU at the PS2's resolution - 0 dropped, 2 late in 2,800; the integrated
GPU at 2x is GPU-bound (24 dropped, GPU 89-92% busy): original resolution
is the setting for it. Next levers, each a larger project: the GS memory
aliasing (~15-20 images reloaded through memory a frame, each a write-back
and a reload), the renderer's Vulkan recording on a thread of its own (a
third of the GS thread), gs.frag's cost per pixel for integrated GPUs above
1x.

### The pixel shader (2026-10-17, day)

gs.frag did everything any draw might need for every pixel. On the Intel
UHD at 2x (the race in real time, all cores):

| | dropped | late | GPU busy |
| --- | --- | --- | --- |
| the whole shader | 493 | 383 | 86% (saturated) |
| a trivial shader (wrong pictures: the bound) | 0 | 14 | 60% |
| specialised per kind of draw | 76-88 | 241-275 | 83-84% |
| ...and bilinear left out (wrong pictures) | 0 | 15 | 72% |
| ...and the alpha test left out (wrong pictures) | 17 | 83 | 79% |
| ...and fog left out (wrong pictures) | 79 | 210 | 84% |

Each pipeline now leaves out what its kind of draw never does (texturing, a
pack's replacement, mipmaps, bilinear, TEXA's expansion, a finer target,
sprite snapping, fog, the alpha test, PABE, logic ops, 16-bit targets: a
specialisation constant). Pictures are the same (the regressions as
recorded). What is left at 2x is mostly the GS's exact bilinear filter
(four texel reads and their wrapping per pixel), then the alpha test's
discards. (Passing the image's size in rather than asking it of the image
measured as noise here; after the gather below it did not - see there.)

At the PS2's resolution, two physical cores and the Intel UHD, the same
build either way (`PS2HOST_GPU_UBERSHADER=1` for the whole shader), two runs
each: whole 36 and 17 late, GPU 55% and 54% busy; specialised 11 and 8 late,
GPU 45% and 42%. The RTX 4070 at 3x: 0 late, as before.

Pipelines: a kind's own is made by a worker thread while the whole shader's
stands in, so none is made mid-frame; the start no longer waits for the
last runs' list (after a shader change it took 12.8 s on the Intel UHD,
the worker has them in the background). A cold driver takes 0.3-0.7 s a
pipeline, so a player's first run would make them as the race meets them:
the port gives the engine the 118 keys its scripted runs draw with
(`port/gpu_pipelines.h`, `tools/make_pipeline_hints.py`), made during the
boot - from an empty cache, one pipeline made on the GS thread (at start),
the rest ready 0.6 s in.

Where it stands: the integrated GPU at 2x went from unplayable (a sixth of
the frames dropped) to about 3% dropped; the PS2's resolution remains the
setting for it. Two cores and the integrated GPU at 1x: 0 dropped, 8-18 late
in 2,800 (run to run).

**The bilinear filter's four reads in one** (later the same day): an RGBA8
image a draw reads has a second view as 32-bit words; where the GS's four
texels lie side by side (almost always), one gather gives them all. The
Intel UHD at 2x: 76-88 dropped -> 47 and 47 (late 241-275 -> 197-214); at
1x and on the RTX 4070 at 3x, as before. Pictures as recorded.

Found on the way: the Intel UHD's pictures differ from the RTX 4070's (the
recorded ones) by a few percent of pixels - before this day's changes too.
In the menus' background map, texels land a pixel over where a coordinate
falls on a texel's edge (the two GPUs interpolate it to different last
bits); most differences are one step of a 16-bit colour. Fixed the same
day: a coordinate is sampled a hair on (1/16384 of a texel, chosen by
measure in both games: a thousandth took Mister Mosquito's pictures further
from the software GS), so one on an edge takes the same texel on every GPU -
the Intel UHD and the RTX 4070 98.92% the same (from 95.75%), Burnout 3
nearer the software GS (race_training 69.09% -> 70.31% of pixels exact),
Mister Mosquito within 0.2 points of where it was; the GPU recordings
re-recorded. Mister Mosquito against this engine (its submodule checked out
at it for the test, then put back): every GPU picture as recorded with the
nudge at 0 - nothing else of the day changes its pictures; with it, most of
its GPU recordings change and are to be re-recorded when the engine is next
tagged.

**The day against the night, the same conditions.** Two physical cores
with half of each taken by the burner (a Pentium Silver's worth) and the
Intel UHD at the PS2's resolution, the night's last build (engine f400c0b)
and the day's, alternately, two runs each: the night's 1 and 1 dropped, 29
and 39 late (99%: 25.4 and 26.0 ms); the day's 0 and 0 dropped, 15 and 7
late (99%: 23.6 and 20.6 ms). (The night's 0 dropped and 4 late with the
burner was a kinder run: the burner's share varies from run to run - only
builds compared back to back say anything.) The menus (the car select) and
the tutorial movie on two cores and the Intel UHD: 4 and 0 late, as before.

What was looked at and left: the GS memory's write-backs and reloads in the
race (~75 and ~80 a frame) are the glow passes' targets over the depth
buffer's memory, drawn through overlapping views (D2/2 and D3/2, a page
apart, and EE/5) - real aliasing: only 6% of the reloads would be spared by
finer page tracking (0.3% of the rows). A direct conversion between images
(one pass instead of a write-back and a reload) would save at most half of
the swizzle's 15-23% of the Intel UHD's time - left for later. Splitting
every draw in two (twice the draws, the same pixels) left the GPU's time as
it was: the number of draws is not what is left at 1x.

**The bilinear path, step by step** (the afternoon; the Intel UHD at 2x,
two runs each, back to back):

| | dropped | late |
| --- | --- | --- |
| the four texels in one gather (above) | 47, 47, 49 | 176-214 |
| the gather's coordinate from the size already asked (one size query a pixel less) | 28, 34 | 167, 188 |
| the bound image's size a push constant (TEXA packed into one word to make room) | 17, 22 | 91, 126 |
| REPEAT over a 2x target by mask (two integer divisions a call less; the 2x race's 48 pictures identical) | 21, 12 | 104, 86 (noise) |

What is left at 2x, by leaving each out (wrong pictures, for the measure
only): the bilinear filter still the most (0 dropped without it), then a
finer target's sampling and sprite snapping (6), then the alpha test (4).

The day's end, the race in real time: two physical cores and the Intel UHD
at the PS2's resolution 0 dropped, 14 late in 2,800 (7-18 over the day);
the Intel UHD at 2x 12-22 dropped (493 in the morning); the RTX 4070 at 3x
0 late, 99.9% 17.5 ms.

**Processors without AVX2.** The port was AVX2 code from its first
instruction: on Intel's Pentium and Celeron of before 2021 (Pentium Silver,
Celeron N) it stopped before it could say why. Now the executable's entry
checks the processor (code built without AVX) and starts
`burnout3-sse4.exe` beside it (`port.py --sse4 build`; the release ships
both). Its cost on two cores and the Intel UHD: 1.30 cores against 1.27,
0 dropped and 8 late against 7; every GPU recording the same through it.

