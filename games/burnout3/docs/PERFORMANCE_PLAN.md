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


## The menus at 60 pictures a second (2026-10-18, night)

Nehalem's "60 FPS Menus" patch (port/sixty_fps.cpp) draws the menus every
blank. Measured (the car select, real time from frame 4000):

* **Two physical cores and the Intel UHD, the PS2's resolution:** 30 and 60
  per second alike - 0 dropped, 8-16 late at 60 (23-47 at 30, run to run);
  0.46 cores against 0.35.
* **The car select's 3D garage is VU1-bound at 60, on the fast CPU too.**
  Its cars are VU1 programs 67-70: ~2.9 million instruction pairs a frame
  (a PS2's VU1 does ~5 million). The EE waits for the VIF1 worker in
  sceGsSyncPath (0x00102EA0: GIF_STAT read, 0x10003020 - found with
  PS2HOST_JOIN_LOG), so a frame is as long as VU1's: ~52 frames a second of
  the garage's 60, 100 of 500 frames past the period.

### Compiled VU code, faster and exact

A benchmark that replays recorded VU1 runs: `--vu-snapshot FILE FRAME
6000` (with `--timeout`), then `--vu-bench FILE --vu-fast` - ns a pair and a
hash of the end state, so a change is fast and exact or not. Two recordings:
the garage (frame 8500 of car_select) and the race (18600 of race).

| change (engine) | garage ns/pair | race ns/pair |
| --- | --- | --- |
| before | 4.96 | 4.40 |
| ClampLanes for CleanLanes: DAZ and FTZ are on in a VU run (VuRounding), so the processor already makes denormals zeros of their sign; only exponent 255 is left to clamp | 4.34 | 4.20 |
| an FMAC of all four fields stores (no blend); VI0 reset only after a block's first pair and COP2 fallbacks | 4.26 | 4.14 |
| Q and P land at a pair's issue only where it can tell (reads Q/P, starts DIV/EFU, copies the VI file, waits) | 4.04 | 3.82 |
| SQ of xyzw as one 16-byte move | 3.97 | 3.75 |
| DIV and SQRT in single precision (one operation, rounded once: the same bits) | 3.93 | 3.67 |

Every state hash the same; the host tests' compiled-against-interpreter
comparisons pass; the regressions as recorded. The benchmark's own state
restore is about a third of its time, so the VU code itself is ~25-30%
faster. In real time: the garage 52 -> 55.8 VU1 frames a second (frames
past the period ~100 -> ~55 of 500); the race on two cores and the Intel
UHD, VIF1's worker 62% -> 55% busy, the process 1.40 -> 1.33 cores.

Tried and dropped (measured): stall tracking left out entirely costs only
~5% (and changes no state in these runs - stalls move timing, not values;
leaving them out where provably unseen is the next lever); ACC held in a
register through MADD chains (no change: the code is bound by its
instructions, not their latency); blocks grouped into one function per 6 or
24 blocks, switching inside it (slower - 4.02 / 3.92 ns - and 24 a group took
MSVC nine minutes a file). clang-cl (2026-10-18, now port.py's compiler
when LLVM is installed): the same VU code 3.33 / 3.02 ns (garage / race,
against MSVC's 4.0 / 3.7), the same hashes.

What is left for the garage at a steady 60: ~7% more of VU1. The larger
levers, each a project: stalls proven invisible per block (Q/P and CLIP
readers out of reach) and left out (~5%); VF registers held in host
registers across a block (fewer loads and stores, the clean checks once);
the VIF's unpacking alongside VU1 on another thread, as the PS2 does it
(~7% of the worker).

### Later the same night

* **A crash at a run's end (and a player's quitting):** StopGsThread
  cleared the GS thread's feed while VIF1's worker could still be running
  what the EE had queued (VU1's XGKICKs: GsWords between its check of the
  feed and its use). Seen at the end of a real-time run in the garage,
  whose VU1 keeps the worker busy to the last frame. The worker is drained
  first now (engine a359684).
* **The vertical blank timer's spin:** with cores to spare it spins the
  last 700 us before each wake, for the blank's precision - and did so for
  every 500 us retry of an interrupt the EE had not taken yet, so while the
  EE waited (the garage: for VIF1's worker) the timer spun nearly all the
  time. Retries sleep now (engine 01a93a3): the garage's timer 32% -> 6% of
  a core at the same pacing; the race 7-8% -> 5-6%.
* **The other games:** Mister Mosquito's six GPU runs and Resident Evil
  Outbreak's seven as recorded with the night's VU changes (their engine
  checked out at the candidate, then put back to their pin and rebuilt).

The night's builds back to back (the starting one, then all of the night's
engine changes), two runs each:

| | starting build | the night's |
| --- | --- | --- |
| two cores, Intel UHD, the race (1x) | 42, 36 late; 99% 28.0, 26.1 ms; 1.40, 1.41 cores; VIF1 62% | 30, 18 late; 99% 25.5, 24.3 ms; 1.33, 1.36 cores; VIF1 55% |
| RTX 4070, all cores, the menus at 3x | 0 late; 99.9% 23.1 ms | 0, 0 late; 99.9% 18.7, 18.9 ms |
| the garage, VU1 frames a second (all cores) | ~52 | ~56 |

(The starting build did worse tonight than its own numbers of the day
before - 36-42 late against 7-18: the machine's state differs from night to
night, so only builds run back to back compare.)

Found, not fixed: at 3x the race's frames ~18,600-18,900 (63,000 GS
primitives a frame, against 27,000-47,000 after) leave the GS thread 70%
busy, its image reloads waiting on the GPU up to 12 ms; some runs (either
build) show a cluster of up to 20 late frames there, others none.

## Splitscreen at 60 pictures a second (2026-10-18, day)

Two players' views double the VU1 work and the GS thread's: the game asks for
a picture every blank in splitscreen too. In real time (i7-13620H, RTX 4070,
2x) both the GS thread and VIF1's worker were 95-100% busy, ~88,000 GS
primitives and ~200 write-backs and reloads a frame (the one-player race:
~70,000 and ~75), 60-76 of 3,000 pictures dropped, the median interval
17.9-18.3 ms. VIF1's worker waited on the GS thread ~16% of its time: the GS
thread was the bound.

The GS thread's work timed alone, the same every run: a capture of the
splitscreen race replayed (`--gs-replay`, `PS2HOST_REPLAY_GPU=1
PS2HOST_REPLAY_SCALE=2 PS2HOST_REPLAY_REPEAT=50`, engine 7cd7ca4), builds
alternated - the machine drifts a few percent between runs. Sampled
(`PS2HOST_REPLAY_SAMPLE=1`, ps2gpu now with symbols): the driver's command
recording ~13%, the bridge's per-primitive work (OnDraw, Triangles, Drew)
~25%, GIF parsing ~15%, image transfers into local memory ~13%.

Done (engine, every Burnout 3 GPU run as recorded after each):

| change | ms a pass (two frames, 2x) |
| --- | --- |
| the start (SMAA, 7afc79c) | 45.8-46.9 |
| image transfers: 32/16-bit pixels moved whole (7cd7ca4) | -2-6% |
| the renderer's batch vertices written in place, not push_back (2da7126) | -3.7% |
| Drew: inside the marked rectangle, what moves alone (42dc6c5) | -1.5% |
| the draw's state made past the fast path only (a81ac9c) | -1.3% |
| texture contents hashed with XXH3 (dc310c6) | -1.7% |
| a batch's state compared byte for byte first (8191ca0) | -1.2% |
| now | ~39 (-15%; the machine drifts: compare alternated) |

Real time, the splitscreen race at 2x: dropped 60, 74 (the start) -> 28,
18 (the first four changes) -> 0, 22, 15 (all of them; the race differs from
run to run in real time); the median interval 17.9-18.3 -> 16.7-16.75 ms;
the GS thread 99% -> 83-89% busy, VIF1's worker 90-97%. The one-player race on four threads at 1x: late 28, 25 -> 15, 7; the
GS thread 57% -> 48-49%; 1.49-1.53 -> 1.43-1.44 cores.

Tried and dropped: the sprite neighbour search (up to 256 back, at 2x and
over) - within noise; Convert's reciprocals hoisted - the compiler had;
Issue landing Q and P inline, not LandNow - exact (the VU bench's hashes the
same) but no faster; the PATH3 queue's release copy - 88% of its words go
in partial releases, which must copy (the sink may hold more); the
splitscreen's pipelines - none the hints lack.

What is left: VIF1's worker (97%) bounds the heavy moments now - VU1 40-56 ms
in two frames' windows; one 50 ms "GPU bookkeeping" span in 45 s,
unexplained. The renderer records ~4,000 draws, ~530 rendering passes, ~150
feedback copies, ~1,000 transitions and ~400 dispatches a splitscreen frame:
fewer passes (the blur's strips, the feedback copies) is the GS thread's next
lever; the open engine pull request's renderer thread (#1) takes the
recording off it altogether.

## Loading, a press to the screen, splitscreen on an integrated GPU (2026-10-19, night)

Engine branch `latency` (1d8d428, 6763888) and game 94d7df3. Every Burnout 3
GPU run as recorded after each change (virtual time is unchanged by all of
them). Benchmarks: the scripted race (and the splitscreen race with both
players accelerating: `--press cross@9300-12900` added), real time, windowed,
frame pacing on so late frames are counted; the night's start (engine
b9373ef) and its end alternated, builds side by side.

**Loading.** FastLoading fast-forwarded the vertical blanks eight times
while the game loaded - the loading screen's spinner ran eight times as fast.
The loading step is now taken again within the frame (RunIopBetweenSteps:
the IOP's slice between steps, up to half a frame), the blanks at their own
pace: the race's load 6.34 s -> 2.61 s, the boot's 5.9 s -> 2.35 s (FINDINGS
"Loading a race"). What is left is mostly the race's sound streams filling
as the sound plays (1.37 s).

**A press to the screen: two frames less** (FINDINGS "A press to the
screen"). The pad block was filled before the window's events were pumped
(a frame old), and the picture was taken before the game copied its
finished frame to the displayed buffer (a frame old). Both fixed: about 33 ms
less at 60 pictures a second. The picture now waits for the copy: the
blank-to-picture delay of the one-player race on the RTX went 0.1 -> 1.1 ms
(median), the price of showing the newest frame instead of the one before.

**Splitscreen on the Intel UHD.** The integrated GPU bounds it even at 1x:
the GS thread waited 41% of its time on the GPU (PresentImageAt's fence).
Its busy figure stays ~88% at any resolution (it clocks down when there is
slack) - dropped frames, not busy %, tell its capacity. Tried and dropped:
barriers without waits (GPU time within noise), doubled 3D/compute switches
(no change), two present slots, no spinning (no power to spare). Found: the
glow's 1024x256 view (read as PSMT8H) reloaded from memory 18 times a
frame at the internal resolution. Views 1024 wide and over are now kept at
1x (closer to the software GS, too):

| run (real time, 3,000 blanks) | start of night | end of night |
| --- | --- | --- |
| splitscreen, iGPU, 2x, both racing | 1 / 21 dropped, 5 / 71 late, GS 100% | 0 / 0 dropped, 3 / 3 late, GS 95% |
| splitscreen, iGPU, 3x, both racing | 138 dropped, 90 blanks lost | 76 dropped, 8 lost |
| one player, iGPU, 2x | 2 late, GS 78%, delay 33.9 ms | 1 late, GS 51%, delay 1.3 ms |
| splitscreen, RTX 4070, 2x | 0 dropped, 0 late | 0 dropped, 1 late |
| one player, RTX 4070, 3x | 0 late, 1.44 cores | 0 late, 1.45 cores |

(delay: the median from a frame's blank to its picture being handed to the
display, the frame pacing's own measure.)

Next levers: splitscreen at 3x on the iGPU is still GPU-bound (draws 73% of
its time); the glow's E0/D2/D3 drawing has FBMSK FF000000 - it never touches
the alpha the 8H reads use - yet still makes the 1024-wide view stale (a
per-channel staleness would drop those reloads altogether); ~810 layout
transitions a frame, which on Intel may each resolve compression.

Later (2026-10-19, the user's playtest): splitscreen played slower than the
console with nothing dropped. The presents are 60 a second whatever is new;
the VIF1 transfers a second in --frame-times count the pictures drawn - 41-48
in splitscreen. The race went one tick a picture where the console's
splitscreen goes two (FINDINGS "Splitscreen's 30 pictures a second"); now two
ticks follow a picture of two blanks: the Intel UHD's splitscreen 45 pictures
and 58 ticks a second (it was 45 and 45: 75% speed). Benchmarks of a game
running below 60 should read the VIF1 transfers, not the dropped pictures.

## High frame rates and the EE's waits (2026-10-22, night)

Measured at 144 pictures a second, 3x, RTX 4070 Laptop, the race in **true
real time**. A `--real-time-from` run is no guide here: its loop budget stays
finite, so the real-time idle path (`LoopHead`'s `realtime_idle`) never runs,
and its profile shows idle loops spinning.

- **RenderWare's packet wait spun.** The loop at 0x001F24E0 calls a function
  each turn (0x00228040, which keeps the largest room seen). The loop is an
  idle loop, but it is put to sleep only at the timer's next millisecond
  look. At 144 its turns took about a third of the EE's time (812 million
  calls to 0x00228040 in a run): the dispatcher 15%, the call stack's push
  6%, the loop 8% and its function 5% of all samples. Now that function
  sleeps first while the loop still waits (`ps2host::WaitForGuestEvent`).
  The EE was idle 26% of the time and is now idle 55-63%; the wait went from
  ~34% of the samples to ~2%; 144 holds, with nothing late.
- **Dispatch.** The call is counted only in a scripted run (the count's
  increment missed the cache each call), and the watch list is a flag.
- **The high-frame-rate take-down.** Its 2 MB transform scan tests each
  candidate in place, and reads the regions whole only every 4th step (in
  between, only the ~850 known transforms): 0.30 ms a step before.
- **The modern effects at 144** (MODERN_GRAPHICS_PLAN.md): the GPU is 60-65%
  busy, against 49-53% for ORIGINAL. Both hold 144.

## High frame rates with the modern graphics and a texture pack (2026-10-22, day)

The case: 144 pictures a second, 3x, every GRAPHICS option MODERN (the
default), the user's texture pack (7,859 DDS files) on, windowed, frame
pacing on; the race and the crash run in true real time (`hf.sh` in the
session's scratchpad: the run's .args without `--virtual-time`,
`PS2HOST_FRAME_RATE=144`, `PS2HOST_TEXTURE_PACK`, profiled from 17,900).
RTX 4070 Laptop, i7-13620H.

Where it stands (engine 1cdae45's pin):

| run | late | longest | VIF1 worker | GS thread | GPU |
| --- | --- | --- | --- | --- | --- |
| race, pack | 2 of 2,600 | 15.2 ms | 70% | 71% | ~55% |
| crash, pack | 3 of 3,100 | 20.2 ms | 70% | 66% | 54-66% |
| crash, no pack | 1 of 3,100 | 13.6 ms | 70% | 65% | |

Per picture (crash, pack): VU1 4.4 ms on average, 6.3 at the 95th
percentile, 7.1-7.4 at the 99th - over the 6.94 ms a picture has at 144. The
GS thread's work is 4.5 ms (its bookkeeping 2.3), the EE idle 3.5 ms of
each picture. The pack costs nearly nothing once its files are read (one
9.7 ms spike of GS bookkeeping in the crash run). **VU1 is the bound.**

The plan, in order:

1. **The profiler's own stalls.** A lane's events were one vector: its growth
   copied the GS thread's ~100 MB at once - 7-16 ms stalls, the profile's
   worst "late pictures". Chunked (done first: the crash run's longest
   interval 20.2 -> 11.1 ms).
2. **VU1 at 144.** Sample the VIF1 worker in a real-time crash run
   (`--sample-vif1`, hotspots.py): which programs and blocks, and what of
   the worker's time is not VU1 (unpacks, XGKICK's hand-over to the GS
   thread, the 650-770 MB a second copied for the worker).
3. **The vertical blank timer** takes 16-23% of a core at 144: its wait, a
   laptop's power and heat shared with the busy threads.
4. **The texture pack's upload** drains the renderer thread's whole queue
   and breaks the GS thread's batch (`UploadReplacement`: `Drain` +
   `Flush`): queued instead, a spike where new textures come into view.
5. **The GPU** at 54-66%: the modern effects' passes at 144 (lower when
   the CPU side is done).

Every change: the GPU regressions (race, race_training, car_select, crash),
then the benchmark pair alternated with the build before.

What was done (engine f57a0c6..aa1257c):

- **The profiler** (f57a0c6): chunked lanes. The crash run's longest
  interval 20.2 -> 11.1 ms; the 16 ms "GPU and memory in step" was the
  profile's own vector growth.
- **The race's start** (3cca51a): the hitch was the GPU, not the pack - with
  the pack off it was the same (10-12 late). nvidia-smi: through the 60-a-
  second screens the RTX 4070 Laptop idles at P5 (600-900 MHz, memory 810
  MHz); at 144 at once it ran 99% busy at 600 MHz and took ~0.5 s to reach
  P0 (2,460 MHz). Into a rate above 60 the rate now rises over 600 ms
  (`PS2HOST_HIGH_FPS_RAMP`, real time only): the race's start 10-13 late
  -> 2-4, those left the 60-a-second intro's own (IOP streaming) and one
  GPU wait of ~20 ms as the GPU leaves P5 (its memory clock's change).
- **VIF** (cbd58b8): microcode (MPG; Burnout 3 re-sends its programs every
  frame, 415,000 a run) compared and copied a run at a time, not a word at
  a time through Word; masked and added (STMOD 1/2) UNPACKs on the one-loop
  path. `VifStream::Words` was 13% of the VIF1 worker's busy time; the gain
  is within the real-time runs' noise (~4%).
- **VU programs** never recorded: five (0x40, 0x7F8, 0x15C0, 0x15D0, a short
  0x0) ran in the interpreter in the crash and mods runs (3.4 million pairs
  a crash run); added to build/vu_programs.txt (game-derived, not in git:
  re-record with `--vu-record` if build/ is lost).
- **The texture pack's uploads**: 224 at the race's start, 0.3 ms at most,
  31 ms in all - not a hitch's cause (on the timeline now:
  "pack: a replacement uploaded").

Tried and dropped: the vertical blank timer's spin (`PS2HOST_SPIN=0`):
the presents' jitter 0.055 -> 0.24 ms and the median interval 6.94 ->
7.00 ms; its "20% of a core" is the CPU accounting's (two runs of one build
read 23% and 0.3%).

What is left: the crash-breaker's explosion (frame ~20,100) - VU1 8-16 ms
for a picture's worth (programs 66/88, entry 0x6A0, 22% of VU1 there,
~1.7 ns a pair; snapshot `--vu-snapshot vu_crash.bin 20060 20000`), and the
GS thread saturated with it (7-9 ms a picture: OnDraw, Gs::Words). Both need
structural work: VU1 faster per pair, or the GS thread's parsing and the
bridge's per-primitive work split.

The day's end, the crash run back to back (the build before the PGO refresh
and the VU programs, then the final one): late 8 and 11, VU1 4.80 and 4.65
ms a picture, the GS thread 5.02 and 4.70 - within the runs' noise. Both
were slower than the same code at the day's start (VU1 4.4 ms, 1-3 late):
the laptop after three hours of runs back to back. Compare builds only side
by side.
