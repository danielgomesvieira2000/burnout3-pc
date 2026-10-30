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

### The next steps, 1 to 3 (2026-10-22, afternoon)

**The rise into a high rate, measured properly.** The race's start (the
scripted race, high fps from frame 17,000, 144, 3x, pack, true real time),
runs alternated:

| rise | late pictures a start | frames' time repeated | dropped |
| --- | --- | --- | --- |
| none (`PS2HOST_HIGH_FPS_RAMP=0`), 5 runs | 9-19, 15 on average | 124 in all | 9 |
| 600 ms fixed, 5 runs | 8-15, 11 on average | 55 | 2 |
| 1.5 s fixed, 3 + 3 runs | 4-6 | 4-6 each | 0 |
| in eighths, each only after one without late pictures (95234a5), 3 runs | 3-6 | 3-6 each | 0 |

What is left are single GPU waits of ~17-22 ms (the renderer's ring full)
in the first half second of the rise: a P5 GPU draws the race's view at
about 60 a second, whatever the rate asked for. The adaptive rise is kept:
as good as the slow fixed one, full rate in 0.6 s where the GPU keeps up, and
it holds back by itself on a slower GPU.

**The other games** on the engine with this morning's changes (aa1257c):
Mister Mosquito 6/6 and RE Outbreak 12/12 GPU runs as recorded (zombies120:
high fps in virtual time, no rise there).

**VU programs** recorded again with `--vu-interpret` (crash, race_mods,
race): nothing more than the five added this morning.

**Benchmarks that repeat.** VU1: `--vu-bench vu_crash.bin` (the explosion,
20,000 runs; ~1.7 ns a pair, half of the bench's time its own restoring).
The GS thread: the explosion's capture (frame 20,081) replayed at 1x,
`PS2HOST_REPLAY_REPEAT=200`: ~6.1-6.3 ms a pass, +-2% run to run (at 3x
it waits on the GPU and wanders 10-13 ms).

**Tried and dropped:** a cache in front of the bridge's image map
(`owned.find` / `count`, 4% of the GS thread's samples in the crash):
6.135 against 6.18 ms a pass - within the noise.

**Step 3, the explosion (crash-breaker, frame ~20,100):** VIF1's worker and
the GS thread are both full there; what one sheds the other must not take.

- *PATH3's words handed over uncopied* (dd70195, reverted fb3da96): the
  worker 4% less busy, the GS thread 2-3% more (it reads the textures cold
  from the worker's copies rather than warm from its ring), and in each of
  three alternated crash runs more pictures late (20/13/17 against 15/6/12).
- *VU1, a pair at a time:* the generator already drops the stall checks
  that cannot stall (`needless_wait`); the clamp is four instructions; a
  lower write of all four fields as one store and one AVX2 store of its
  times: 60.6-61.2 against 60.1-60.7 ms a bench pass, the same state hash -
  no gain, dropped. Trap: an edit to vu_core.h changes every compiled block,
  which then loses its PGO profile - compare such changes as two `--no-pgo`
  builds, or refresh the profile for each.
- *VIF decoding on the EE* (idle half the time): weighed, not done - most
  of an explosion's unpacking is V4-32 runs, a copy either side, so it would
  move little; and the game chains its programs with MSCNT (5.9 million
  against 0.7 million MSCALs), registers carried from run to run, so VU1
  runs cannot go to two threads exactly.

What would change the explosion: VU1 code with the VU's registers held in
the host's across a block and its timing worked out ahead (a real
recompiler, a project of its own), or the GS thread's parsing split from the
bridge's bookkeeping onto two threads (the parse is ~23% of the thread,
OnDraw ~30%).

### The VU1 clamps and the GS thread split (2026-10-23)

**VU1's clamps** (engine a58fe2a): blocks start knowing which fields are
clean from every known way into them (engine/docs/VU_RECOMPILER.md, step
8). The explosion's VU bench 43.5 -> 42.4 ms, the same hashes. The clamps
left are mostly of registers a run starts with, left by the runs before.

**VU1's flags**, weighed and not done: of 339 million pairs the bench runs,
7.1% still work out MAC (each one read later, so needed), 2.5% read it. The
hand-over between blocks (the queue) is all that could go: 2-3% at best,
for the most delicate timing of all. A run without flags was no measure -
the loops branch on MAC.

**The GS thread split** (PS2HOST_GS_SPLIT=1, off by default): the GIF's
decoding (tags, PACKED, the vertices; engine host/src/gs_decode.h) on a
thread of its own in front of the GS thread, a second ring between them.
Exact: the explosion's replay gives the same local memory, trace and GPU
pictures, and the GPU runs draw as recorded with it. The replay (1x, 200
passes, a sync each pass) 7.04 -> 6.16 ms a pass (-12%).

In true real time (the crash run, 144, 3x, the pack, two runs each, alter-
nated): the GS thread 74.5-74.8% -> 69.9-70.4% busy; late pictures 4, 4 ->
3, 5; dropped 5, 2 -> 10, 7. In the explosion's late pictures the decoding
waits on the GS thread 15-19 ms: the GS thread is still full there - with
the bridge's work per primitive (OnDraw 23% of it, Drew, the texture
cache), not the decoding. So it stays off; the bridge's per-primitive path
is the GS thread's next lever (~98,000 primitives a picture there).

## Low-power machines again (2026-10-23/24, night)

Goal: playable on low-power PCs, very optimised. Measured with the same
benchmark (`PS2HOST_CORES` masks from the start, the PS2's own resolution,
`PS2HOST_GPU=Intel` for the laptop's integrated GPU), the race and now the
crash run's pile-up (frames 18600-19200: cars and debris all over the
picture - the heaviest stretch of the scripted runs, heavier than the
explosion).

**Where it stands** (late pictures in ~2,700; 0-2 dropped everywhere):

| | race | crash (pile-up) | road rage | splitscreen | menus |
| --- | --- | --- | --- | --- | --- |
| 2 cores, no HT (0x5) | 7 | 39-64 | | | |
| 2 cores, no HT, Intel GPU | 1-3 | | | | |
| 2 cores + HT (0xF), Intel GPU | 1 | 9-19 | 0 | 2 | 1 |
| the same, 40% of each core taken | 1 | 25 | | 0 | |

So a low-end i3 with an integrated GPU plays everything; what is left is a
two-core CPU without hyperthreading in a crash's pile-up, where the whole
process wants ~1.3 cores on average and more in bursts.

**The pile-up's threads** (all cores, `--frame-times` CPU per thread, the
renderer's thread now named): EE 40%, GS thread 38%, VIF1's worker 39%, the
GPU renderer 27%, the present thread and the blank timer 2-4% each. In the
two-core late pictures the GS thread is busy 45-55 ms (about three frames)
while VIF1's worker waits on it. In virtual time the EE's thread (EE, VIF1
and VU1 together) is 12.4 ms a frame there: VU1's compiled code 44%, the
game's own code 19%, VU0 macro instructions 7.4% (+ microprograms 2.2%),
VIF 6%, copies 6%, the IOP 3%.

**Done.**
- *The EE in the VU's rounding throughout* - done and undone (engine
  8f8cb74, 6202b4e): macro mode sets the SSE control word toward zero and
  back for every COP2 instruction, a fifth of MacroFmac. The EE thread's
  fibers run in it throughout instead: the pile-up in virtual time 9.49-9.68
  -> 9.23 s (-3.5%). But the FPU helpers' double arithmetic then rounds
  toward zero too (their result exactly truncated where the double's
  rounding to nearest crossed a single's boundary, about one operation in
  2^29), so every race's trajectory drifts from the recorded runs, and on
  two cores the pile-up showed more late pictures with it (45/57/56 against
  21/17/33 in the same build with PS2RT_EE_ROUND_NEAREST, 43/45/36 before) -
  each a different pile-up, no clean measure. Undone; to try again, keep
  the helpers' results bit for bit (switch the control word around them,
  or only where the lifted code runs COP2 instructions back to back).
- *Tools*: `--sample-renderer` (the renderer's thread sampled), the thread
  named "GPU renderer" in the CPU per thread, `--gs-replay` printing its
  thread's own CPU per pass, `PS2HOST_CAPTURE_FRAME` used for a capture of
  the pile-up (a GS-bound replay: 16.25 ms of the GS thread's CPU a pass of
  two frames with the profile-guided build, 20.3 ms without; ~120,000
  primitives a frame).

**Tried and dropped** (no gain, or worse):
- OnDraw's bounds computed once with inline floor/ceil, and the threaded
  renderer's same-state primitives merged into one record a run: the
  explosion's replay 6.0 ms both ways (two `--no-pgo` builds: noise).
- The GS ring's producer asleep after 100 us instead of spin-yielding (two
  cores, three runs each): late 57/71/86 against 53/61/62 - worse; the spin
  is cheaper than a wake.
- `PS2HOST_SPIN=0` on two cores with hyperthreading: late 13/26/18 against
  9/11/10 with the spin - the four-logical-core rule stays.
- MacroFmac forced inline: +5.6 MB of code, 9.17 s both.
- Drew remembering its four last page rectangles: misses 30,000 -> 8,000 a
  pass of the pile-up, the GS thread's CPU 20.3-20.5 ms both (`--no-pgo`) - the sampler's
  share for Drew was the cost of a cache miss landing there, not the work.
- `PS2HOST_GS_SPLIT=1` on two cores with hyperthreading: late 8/17/3 against
  19/9/13, +5% CPU - within the noise; left off.
- Without the operand clamps (inexact, a measure only) the explosion's VU
  bench is 38 ms against 42.6: the clamps are ~10% of it, but the ways to
  skip them were tried before (clean-entry versions, a record of clean
  registers) and cost more than they saved.

**Pitfalls.** The sampler is unreliable on the worker threads in real time
(the GS thread and the renderer show 87-91% of samples waiting while they
use 27-38% of a core); measure them with `--gs-replay` (now with its CPU)
or the CPU per thread. The replay's wall time can be the renderer's thread
- look at the thread's CPU.

**What would change the pile-up on two cores.** The work itself: VU1 code
with registers held in the host's across a block and its timing worked out
ahead (a project of its own), and the GS side's ~80 ns a primitive (decode,
Kick, OnDraw) - per-primitive trimming did not show; batching whole strips
through the bridge would.

## The GS thread and the renderer by taking parts away (2026-10-24)

The sampler misleads on these threads (and on compiled VU code at the
instruction level: two "hot" instructions that a change to them showed were
nothing). So: measured by switching a part off (a measure only, never
kept) and reading each thread's own CPU from the pile-up's replay
(`--gs-replay`, now with the GS thread's and the renderer's CPU a pass;
`PS2HOST_REPLAY_SAMPLE=renderer` samples the renderer's thread).

The crash's pile-up (frame 18860, two frames, ~245,000 primitives), the GS
thread 19.9 ms a pass without the profile: GIF decoding 6.8, vertices and
Kick 1.5, Draw and the GPU bridge 11.0 - of it OnDraw 9.3 (Drew 1.5, the
hand-over to the renderer 1.9), the replay's own 0.65. The renderer's
thread: 11.6 ms, 8 of them the primitives one by one.

**Done.**
- *GPU vertices 32 bytes, not 64* (engine bb742b0): a sprite's range in a
  stream of its own, read only on a target drawn larger. Half the bytes
  built, copied and fetched. Exact (pictures and memory at 1x and 3x).
- *Runs of primitives handed over as one* (engine 4029077): consecutive
  primitives of the last state one record; the renderer's thread one
  DrawTriangleRun, taken at once where nothing is done a primitive at a time
  (no feedback, shader blend, two-pass alpha test, sprite or flat-quad range
  on a larger target). The pile-up's renderer 12.1 -> 9.5 ms a pass (-21%),
  the explosion's 4.0 -> 3.05 (-24%); exact; eight GPU runs as recorded.
- *OnDraw without gathering registers where none was written*: Gs::Apply
  counts its writes; the GS thread 19.48 -> 19.19 ms.

With the profile refreshed, against the day's first build (both profile-
guided): the pile-up's GS thread 16.0 -> 15.3 ms a pass; in real time on
two cores (the crash run, three each, alternated) late pictures 42/38/50
-> 25/31/23.

**Tried, dropped.**
- *VU1: clamp-free block bodies behind a dirty-register mask* (one test a
  block, the mask kept at loads and exits): exact (hash, host tests), but
  the fast bench 42.2 -> 47.2 ms - twice the code costs more than the
  clamps. With every register assumed clean (inexact, the ceiling) it was
  -7.5%; without any timing bookkeeping (inexact) -7%. The bench itself
  is about half its own snapshot restoring.
- *`discarded_` not stored* for a compare into VF0 whose flags come from
  the result: no change. `__restrict` on the blocks' Vu and run state:
  within noise (kept).

### The GIF decoding (2026-10-24, later)

Taken apart the same way (the pile-up's replay, --no-pgo): with nothing
decoded the GS thread is 0.65 ms a pass (the replay's own); IMAGE's
transfers ~1 ms; building the vertices ~0.4; the decoding of the rest -
tags, PACKED's registers, their writes - ~5.2 ms for ~1.4 million
quadwords, ~3.7 ns each.

**PackedRun** (engine 319101b): where no trace wants each quadword's
source, a PACKED tag's whole quadwords in a loop of their own - each
register's descriptor taken from the tag once, the place in the loop in
locals, no source address noted a quadword. The GS thread 18.9 -> 17.2 ms a
pass (-9%); the same pictures and memory at 1x and 3x, and through the GS
split; eight GPU runs as recorded. With the day's work the pile-up's GS
thread is 19.9 -> 17.2 ms (-14%) without the profile.

What is left in OnDraw after the bounds (~6.5 ms a pass): the hand-over
(1.9), Drew (1.5), and ~3 ms spread thin - every primitive goes the fast
path's cheap way (DrawTrianglesAgain), so no one part: the records' ~27 MB
a pass written for the renderer, and its thread running beside it.

## The day's list, and the engine's share (2026-10-24, day)

**The macro code's rounding** (engine 465d0c2): each COP2 macro
instruction switched MXCSR to the VU's rounding and back. The lifter now
puts a run of them - only moves to and from the VU's registers between, no
label - in one scope that switches it once (lifted::MacroRun, the run's
instructions Cop2Run): 232 runs, 848 instructions. Exact (every GPU run as
recorded). Alone, before the profile was made again: the race in virtual
time -0.3% and -0.2% (bench.py, three each, twice) - inside the spread.

**Training what players run** (game 5765af7): no PGO run had the modern
graphics, a high frame rate or a texture pack, and what no run reaches is
laid out cold. race_modern - the race with the modern graphics, 120
pictures a second from frame 11,500 (`--setting high_fps_from`, so the
presses land as at 60) and 2x - is deterministic and recorded, and a
training run, with this PC's pack (game.json's pgo_env). The profile made
again, against the build before it (bench.py, three each after a warm-up):

| | before | after |
| --- | --- | --- |
| the race, virtual time | 46.74 s | 46.09 s (-1.4%) |
| race_modern, virtual time | 45.82 s | 45.30 s (-1.1%) |
| VU bench (the crash) | 42.6 ms | 40.8 ms (-4.2%) |
| the pile-up's replay: GS thread / renderer | 14.69 / 8.59 ms | 14.53 / 7.97 ms |
| the explosion's replay: GS thread / renderer | 5.31 / 2.66 ms | 5.00 / 2.97 ms (the renderer's spread 2.5-3.3) |

In true real time - the crash at 144 pictures a second, 3x, the modern
graphics and the pack, the old profile and the new alternated, twice:

| | late / dropped | 99.9% interval | GS thread busy | VIF1 worker busy |
| --- | --- | --- | --- | --- |
| old profile | 0/0, 1/0 (18.5 ms) | 10.15, 9.57 ms | 58.2%, 59.2% | 54.8%, 54.8% |
| new profile | 0/0, 1/1 (10.7 ms) | 9.59, 9.64 ms | 57.4%, 57.6% | 54.0%, 54.2% |
| new, GS split | 0/2, 1/2 | 10.34, 10.44 ms | 64.0%, 64.2% | 56.4%, 56.2% |

The crash's 1,700 pictures at 144 a second with the pack now come with one
late picture or none either way; the new profile's threads a little less
busy.

**The GS split** (PS2HOST_GS_SPLIT=1) again, now the decoding and OnDraw
are lighter: in true real time (the table above) the GS thread is busier
with it (64% against 57%) and two pictures were dropped in each run - the
hand-over between the two threads costs more than the decoding it moves.
It stays off.

**bench.py** (engine 6cf7567): the deterministic benchmark - GS replays
(the GS thread's and the renderer's CPU a pass), VU snapshots, virtual-time
runs; medians and spread, --json and --compare.

**Not done, and why.**
- *The final GPU vertices built on the GS thread*: GsVertex and the GPU's
  PackedVertex are both 32 bytes - the records would be no smaller, and the
  renderer's copy (its thread has room) would move onto the GS thread, the
  one that limits.
- *The call dispatch*: a direct-mapped cache, one indirect call and the
  guest call stack's push and pop (widescreen.cpp reads it) - nothing
  worth its risk.
- *Drew* (~1.5 ms of the pile-up's GS thread): its common path is a few
  compares a primitive; putting it off across a run of primitives would
  need every reader of the targets' bookkeeping to catch it up first.
- *VU1 superblocks, VF registers held across blocks*: the blocks are short
  and branchy, and the ceilings measured on 2026-10-24 (all clamps gone
  -7.5%, no timing at all -7%, both inexact) bound what they could win - a
  project of days for a few percent of one thread.
- *An integrated GPU at 2x*: needs the iGPU chosen by hand; not measured.

## The roadmap from here (2026-10-26)

Where it stands: one player at 60 never drops a picture on a desktop; the
refresh lock (engine d1a5aa7) took away the drift between the game's clock
and the screen's (592 doubled and 997 missing refreshes in a race at 144
before, none once locked). What is left drops pictures only where a thread
runs out of time: the VIF1 worker (VU1) and the GS thread in a crash's
explosion or pile-up at 144 with the modern graphics, and every thread on
two cores. The levers the sampler and the ablations named are spent down
to a few percent each (above); what is left to win is structural - more
threads for the work that has one, or less work on the thread that limits.

Every step as before: a bench.py baseline first (`--json`, then
`--compare`), exact (GPU runs as recorded, VU hashes, host tests), PGO
refreshed before a real-time comparison, PGO against PGO.

### First: what players see (an evening)

1. **A player's own numbers.** PS2HOST_REFRESH_LOG and PS2HOST_HITCH as a
   PC SETTINGS row or an ini switch, written to a log beside the
   executable: doubled and missing refreshes, late pictures and which
   thread was busy before them. Reports then come with the machine's own
   evidence (the VRR stutter reports came with none).
2. **The refresh lock on a VRR screen**: a player's log, the lock giving
   up ("follows the pictures") or not. Its give-up has only host tests.
3. **Frame pacing on by default?** Off for the least delay; with the lock a
   paced picture goes in the middle of a refresh (no slips in a race) for
   about half a refresh more. Measure the delay both ways (the timeline's
   "due after its blank") and decide with the user.
4. **The first minutes**: pipeline compilations and the texture pack's
   first loads in a fresh install (no pipeline cache) - a hitch list with
   the profiler, then the missing pipeline hints recorded.

### Night projects, biggest first

**A. VU1 on more than one thread** (the largest; nights). The VIF1 worker
runs every VU1 program of a frame in order, on one thread - the bound at
144 and in the pile-up. Most of a frame's programs are separate objects:
an MSCAL whose inputs (VU memory, VI and VF registers) were all written by
the same packet's unpacks. Plan: (1) measure - for recorded frames, the
share of programs whose reads are covered by their own packet's writes
(the interpreter can mark every read's writer); if it is small, stop.
(2) Run those on a pool of workers, each with its own VU state, their GIF
output kept in order (the XGKICKs in program order, as PATH1 goes now);
anything that reads what an earlier program left runs where it is now.
(3) Exact by construction (same inputs, same programs, same order out);
checked by the VU hashes and every GPU run. Worth it only with cores
spare - off on two.

**B. The GS thread's hand-over to the renderer** (a night or two). After
the decoding work its largest part is OnDraw (~6.5 ms of the pile-up's
17): the records written for the renderer (~27 MB a pass) and Drew's
bookkeeping. A compact record - the state once a run, vertices as they
came - and the renderer expanding them, moves work off the thread that
limits onto one with room (the renderer's ~8-10 ms). Measure by ablation
first (the records not written: the ceiling).

**C. The renderer on the GPU's side** (nights; after B). Its thread still
walks the primitives; with B's compact records a compute pass could build
the vertex buffers and the draws from them (indirect draws), the thread
only ordering state changes. Pays off with B; on its own, little.

**D. Two cores** (a night). On two cores without HT the crash's pile-up
still runs late (25-31 late pictures). Five busy threads on two cores: try
the VIF1 work on the GS thread there (one hand-over less, no
context switches between them), the renderer's on the GPU submission
thread, and the timer's priorities - each switched on by core count,
measured with lowbench.sh.

**E. PGO for what players run** (an evening, again after each project).
race_modern was added; still missing: the crash with an explosion,
splitscreen, a mod-heavy race, a texture pack at 3x. What no training run
reaches is laid out cold.

**F. A nightly benchmark** (an evening, then for free). bench.py over the
replays, the VU snapshots and the virtual-time runs, compared with the
last night's JSON: a slow change found the night it lands, not a week
later.

### Not again, unless something changes

The GS decode split (the hand-over costs more than the decoding it
moves), VU1 clamp-free or dirty-mask bodies (code size), superblocks and
VF registers across blocks (ceilings -7% of the VU bench), zero-copy
PATH3, frame skipping (Burnout's own fixed-step loop already keeps its
speed; a skip only drops pictures), stepping the frame rate down to hide a
hitch.

### A, measured first: VU1 on more than one thread (2026-10-26)

The measure (engine, `PS2HOST_VU_DEPS=1` with `--vu-bench SNAPSHOT`;
`PS2HOST_VU_DEPS_OUT=file` writes the graph): each run of a VU1 snapshot
replayed on the interpreter from its own start - the data memory it reads
before writing it (the interpreter's accesses), the registers and flags it
reads (run again with each changed to random bits: what it sends, leaves
in memory or in the other registers differs), what it writes; the VIF's
writes between runs nobody's. A run depends on the last writers of what it
reads; weighted by its pairs. MAC and STATUS as a run leaves them are left
out (fast mode, the game's, does not keep them). Three snapshots, 20,000
runs each: the crash's explosion (frame 20060), its pile-up (18860), the
race (12500; ~1,150 runs a frame).

| | explosion | pile-up | race |
| --- | --- | --- | --- |
| exact, any edge: the most side by side | 1.80x | 1.18x | 1.16x |
| exact, 4 / 8 workers | 1.28x / 1.35x | 1.08x / 1.08x | 1.06x / 1.07x |
| predicted edges gone, 4 / 8 workers, half a frame ahead | 4.0x / 7.5x | 4.0x / 5.1x | 3.9x / 5.1x |
| predicted edges gone, 4 / 8 workers, 64 runs ahead | 2.6x / 3.0x | 2.3x / 2.5x | 2.4x / 2.7x |

As the programs run, VU1's work is one chain: 3.1 of the race's 3.35
million starts are MSCNT, a program going on where it stopped, and each run
reads what the one before left - VI13 and VI14 (an output buffer's two
pointers, swapped every run), VI15, VI12, VF28-31 (a matrix), and memory.
Most of those reads are the value the same program began its previous run,
or the run before that, with: predicted (VF28-31 ~90%, VI15 and VI12 99%,
memory 80-83%). With them taken as predicted, the longest chain is 1/100
to 1/160 of the work.

So A is speculation or nothing: exact runs side by side win 6-35%. With
each run started on predicted inputs and checked when its writers are done,
4 workers could take VU1 to a quarter of its time - but it needs what each
run reads known while it runs (the compiled code tracking its reads, or a
whole-state compare that fails far more often than the reads do), a copy of
data memory a run (~1,150 x 16 KB a frame: 2.6 GB/s at 144), the runs'
XGKICKs held and sent in order, and a fall-back to the chain where a check
fails. Days of work, its overheads not measured; and with VU1 a quarter,
the GS thread is what limits the explosion at 144.

Found on the way: Vu::Kick counted its runs from an address it had not
wrapped - an XGKICK from VI past data memory's end read past the memory
(no recorded program does it; a perturbed one did). Wrapped now.

### A, modelled with a predictor a port could have (2026-10-29): it does not pay

**Why model again.** The 2026-10-26 estimate (4 workers, 2.3-4x) counted each
dependency *edge* as predicted when its value matched. A run is only right if
*every* field it reads matches. `PS2HOST_VU_SPEC=livein[,workers,window,overhead]`
with `--vu-bench` (engine, `Vu::Speculate`) models that:
- Each run, or segment of runs worked through in order, starts on a predicted
  state. The prediction is the state as committed, plus the VIF's writes
  since (known exactly), plus what each run still in flight is taken to
  write: its program's latest committed run with the same VIF write pattern
  before it, its "donor".
- It is right if that matches the true start on every field the runs read
  that neither they nor the VIF wrote first. Data comes from the
  interpreter's reads, per field. Registers come from each program's
  live-ins: `engine/tools/vulivein.py`, a static liveness analysis that
  counts Q, P, MAC, STATUS and CLIP as read anywhere they are read (their
  writers land later).
- **The rule is exact.** Every run or segment judged right was run for real
  from its prediction and gave the in-order result: XGKICKs, written fields
  and registers. That held for 1,674 of 1,674 in the race and 2,113 of 2,113
  in the explosion.

**Results**, 20,000 runs each, 400 pairs' cost a unit:

| | race | explosion |
| --- | --- | --- |
| single runs right (4 workers, 64-run window) | 8.4% | 10.6% |
| segments of 4 / 16 runs right | 0.0% / 0.1% | ~0% |
| segments cut where a run alone would be right (an oracle), ~10 runs | 0.1% | 2.5% |
| speed-up, any configuration | 1.00x | 0.99-1.00x |

**Why.** Burnout's VU1 stream passes small, computed state from run to run:
- **Run pairs.** 14E0 runs as A then B. A converts the VIF's integers in
  place (0x0A1 holds `4780xxxx`, an UNPACK with an offset row; it comes back
  as floats) and B reads them (0x0A1, 0x13D, 0x09D).
- **A per-object matrix memo.** 14E0 starts with `ilw.w vi01, 965(vi00)`;
  `ibne`. 0x3C5.w is "matrix done": the VIF uploads a new object's matrix
  with w=0, the first run computes vf28-31 and stores 1, and later runs reuse
  vf28-31. Objects are 2-4 runs long.
- **Shared toggles.** 0x3F5.y (0/2) is read by 1548, 1910, 1318 and 1398;
  0x3EE.w is another.
- **What the predictor managed.** The donors and VIF signatures fixed most of
  the flag misses (0x3C5.w: 4,824 to 1,383; 0x3F5.y: 1,604 to 172). What is
  left is *computed data*: vf10, vf28-31, and B's inputs that A produced.
  No value predictor can guess those. Segments covering whole objects would
  need the runs before them done as well.

**Decision:** speculative VU1 threads are dropped for Burnout 3. The exact
side-by-side ceiling stays as measured on 2026-10-26 (1.06-1.28x).
- Programs whose items are independent within a run, such as 06A0's
  particles and 14E0's vertices (RUNTIME_PLAN, 2026-10-29), would be split
  *inside* a run instead: SIMD across iterations, or a GPU path with roadmap
  C. Or VU1 is made faster on one thread.
- Kept as tools: `PS2HOST_VU_SPEC` (plus `_SEG`, `_PRED=program`, `_BACK`,
  `_WRITES=0`, `_SIGNATURE=0`) and `vulivein.py`. Another game's stream may
  be more independent, and the model answers that in one run.

### B, measured: the GS thread's hand-over (2026-10-27)

The pile-up's replay (`--gs-replay`, bench.py; without the profile: the GS
thread 18 ms a pass), each part switched off in turn (a measure only):

| switched off | GS thread | the part |
| --- | --- | --- |
| nothing | 17.97 ms | |
| all of OnDraw | 8.91 | 9.06 ms: half the thread |
| OnDraw past the vertices' conversion | 10.00 | conversion 1.1 |
| past the bounds and the scissor's cull | 11.72 | bounds 1.7 |
| the fast path past its choice | 14.38 | its tail 3.6 |
| Drew on the fast path | 15.94 | 2.0 |
| the records for the renderer (Same) | 15.78 | 2.2 |

What the replay draws: 276,131 triangle-strip primitives, 7,301 sprites,
a few hundred points and lines; 252,036 by the fast path, 10,107 the whole
way (~300 ns each: ~3 ms). The registers stay the same over stretches of
~36 primitives (7,253 stretches; 5,054 of 16 or more).

**Tried, no gain (dropped).**
- *Strips sent as strips*: a triangle continuing the one before sends its
  third vertex only (a third of the bytes), the renderer's thread making
  the triangles again: 18.28 against 17.97 ms - the records' cost is the
  call and bookkeeping a primitive, not the bytes.
- *The fast state kept through its own Drew* (g_mutations moved by the
  fast path's drawing alone): 392 of the 10,107 whole-way primitives
  saved, no change in time. What sends them the whole way, by where
  g_mutations moves: Use 1,826, SyncWrite 1,581, Sync 1,571, LoadClut
  1,343, Drew 1,138, MarkStale 1,117, CachedTexture 934, WriteBack 320 -
  6,464 of them have the fast state's very registers.
- *The extremes found once* (one loop for the bounds and the cull):
  within the noise.

**What is left, and its size.** Nothing here is one large part: the
thread's OnDraw half is ~35 ns a primitive spread over conversion, bounds,
checks, the record and Drew, and ~3 ms in the whole-way path.
- *The primitives of a stretch handed over as one* (the GS model holding
  them until anything but a vertex comes; the bridge one tight loop, one
  record, Drew's quick path inline): ~3 ms of 18 (-15%), a refactor of the
  GS model's hand-over and the bridge's fast path.
- *g_mutations per resource, not one count*: two in three whole-way
  primitives could stay fast (~2 ms), in the bookkeeping that decides when
  memory and the GPU's images agree - the riskiest code in the bridge.
- *Drew's rectangle as the dirty pages' largest*: more primitives on its
  quick path, but its quick path already skips updating the drawn rows'
  end; changing when it applies changes the bookkeeping, not only its
  speed.

### B, tried: a stretch handed over as one (2026-10-29)

Two forms, both exact (car_select, race_training, race, crash as recorded),
both slower on the replays (no PGO, 5-7 passes; before: pile-up GS thread
17.50 ms, explosion 5.62):

- *The GS model holding the stretch* (its primitives' vertices copied, handed
  over at the next register, image data, privileged write, frame end, or
  when the GS thread caught up with its ring; the bridge then one primitive
  at a time): 19.38 / 6.41 ms (+11%, +14%) - the copy alone is ~7 ns a
  primitive, ~2 ms of 280,000.
- *The bridge holding its fast path's vertices* (the GS model only saying
  where a stretch ends; one DrawTrianglesAgainRun record a run): 18.28 /
  6.25 ms (+4.5%, +11%). The threaded renderer already merges a run's
  primitives into one record (Same, 2026-10-24), so a record a stretch saves
  only an append's bookkeeping, while holding the vertices copies them twice.

Reverted. What is left of B is the per-primitive work itself (conversion,
bounds, checks, Drew): a tight loop over a stretch only pays where it does
less per primitive - each strip vertex converted once rather than three
times (~0.7 ms of conversion's 1.1), the frame's and scissor's constants and
the fast checks once a stretch - and needs the stretch's vertices without a
copy (the GS model's strip kept as a stream). An estimate of 1-1.5 ms (5-8%),
not measured.

## Hitches: a census, and the GPU's clocks (2026-10-27)

True real time, windowed, the scripted race and crash runs (hf.sh), the
profile from frame 11,900 (the pictures' pacing) and, with frame pacing off
(the default), `PS2HOST_REFRESH_LOG` and `PS2HOST_HITCH` (a present more
than 25 ms after the last):

| run | late / dropped | longest |
| --- | --- | --- |
| race, 60, original, pacing off | 0 (blanks) | 17.98 ms |
| crash, 60, 2x, pacing off | 0 (blanks) | 18.09 ms |
| race, 144, 3x, the pack, pacing on | 0 / 0 | 9.10 ms |
| crash, 144, 3x, the pack, pacing on | 2 / 5 | 11.40 ms (the explosion: VU1 12 ms) |

The races themselves were clean; what stuttered was elsewhere - 4-10
presents a run 26-103 ms apart in the menus, at a race's start and at the
crash event's start, the same with the pipeline cache gone or kept (not
the shaders). The crash event's start, profiled: the GPU renderer's
thread waiting on a full ring of submissions for 25-40 ms - the GPU itself
slow. nvidia-smi every 50 ms: through the loading screen the RTX 4070
Laptop sat in P8 (360 MHz, its memory 405 MHz); the scene's first frames
ran half a second at 98% in P5 (630 MHz, memory 810), then P0 (1,980 MHz,
memory 8,001) at 22% - the same work, a tenth of the memory's speed.

**NVIDIA's low-latency boost** (engine 8901b19, bd30d1d; game 1a326f4):
VK_NV_low_latency2 where the driver has it, the swapchain made for it,
the sleep mode set with its boost (its waits never made). PC SETTINGS >
VIDEO > GPU BOOST, on by default (off: a laptop's battery).

| | without | with |
| --- | --- | --- |
| crash event's start, 60 2x: late | 2, 4 | 0 |
| race run at 60: presents over 25 ms | 4, 9 | 2, 2 (startup, title: two blanks, the game's own) |
| race start, 144 3x the pack: late | 16, 7 | 2, 2 (the profile's first frame; one 11 ms VU1 picture) |

The GPU then stays in P0-P4. Not covered: AMD and Intel GPUs (no such
extension); a keep-warm load of our own would be their equivalent.

Also: the high-fps trace's "turn" for a quaternion camera was its three
values' dot product, a sign flip read as a 138-degree turn - now the
rotation's angle. Its large "jump"/"twitch" lines in a race run are the
race's own resets after a crash (cuts, shown as they are). The texture
pack reads its files in the background (no stall at a new texture).

## Smooth, low latency, low power (2026-10-27/28, night)

**MODERN texture filtering by the GPU's sampler** (engine bee1d4b). The
game's textures were filtered by hand in gs.frag (a gather and the GS's
sixteenths), a texture pack's by the GPU - the pack-off crash start at
144, 3x saturated (48-58 late, 120 a second). Each MODERN level is now one
hardware bilinear fetch (REPEAT's seams two, mixed). The weights stay
the GS's sixteenths and the values truncated: finer weights gave alpha 1
where the GS has 0, and an alpha-tested polygon showed.

| 3x replay, a picture | MODERN before | MODERN now | ORIGINAL |
| --- | --- | --- | --- |
| explosion | 7.35 ms | 6.93 ms | 6.79 ms |
| crash start | 7.00 ms | 6.41 ms | 6.25 ms |

Real time, pack off, crash start at 144 3x: 0 late at 144, then 5 late
(was 48-58, both stepping to 120).

**The occlusion's distances at its own resolution** (engine 6f9d7f5): a
pass writes them once, the spiral and the sun's march read them, 0.9 ->
0.75 ms of a 3x explosion picture. The rest is arithmetic: the main pass
0.57 ms (the sun's 20-step march 0.17), the blurs and the apply 0.1 each.
All four effects are about 1.4 ms of the explosion's 6.9 at 3x.

**Refresh lead** (engine 8e4092a). The locked blanks fell on the screen's
vblanks, and a picture done on the GPU a few ms after its blank waited
nearly a whole refresh. An empty fenced submit after each present tells
when the GPU finished. The blanks now lead the screen's by the longest
blank-to-done of two seconds (+10%, +1 ms, modulo the refresh), when that
beats the blanks on the vblanks. Pacing off only (the default).

| fullscreen, race | blank to shown before | now |
| --- | --- | --- |
| 60 Hz screen, 60, 1x (3 ms to done) | 16.66 ms | 5.9-7.3 ms |
| 144 Hz screen, 144, 3x (5.5 ms, peaks 8.5) | 7.3 ms | 7.3 ms (no lead: the next refresh is sooner) |

No picture went to a refresh that had one, and none were late.
`PS2HOST_REFRESH_LOG` prints `[latency]`; `PS2HOST_REFRESH_LEAD=0` turns
it off.

**FRAME RATE never above the screen's refresh** (engine 920a833): 144
asked of a 60 Hz fullscreen mode drew 120 pictures a second, MAILBOX
showing 60. GS thread 100%, 760 late, 83 ms blank to shown -> 60, GS 19%,
none late, 10 ms. **Minimised, the game's own rate** (engine 55b9dbd;
2 cores -> 0.7 at 144; not yet tried by hand).

Measured, nothing to take:
- The timer's last-700 us spin: the high-resolution sleep wakes 300-600 us
  late (p90 600 us, p99 1.4-4 ms), so the margin is right.
- The crash start's write-backs are small (32-128 wide).
- The EE at 144 has no hotspot (the high-fps CSV trace itself is 9% of it -
  a measuring cost, off for players).
- At 144 the CPU is 2.8x 60's (EE 56%, VIF1 53%, GS 56%, renderer 34% of a
  core at 1x): every picture runs the game's drawing.
- Frame pacing ON holds a picture to 6 ms after its blank, then the middle
  of a refresh - 10.4 ms at 144. It's opt-in (smoothness), left as it is.

## Phones: the first night (2026-10-28, night)

The player's Pixel 7 Pro (Tensor G2: two Cortex-X1, two A78, four A55;
Mali-G710 MP7) ran races at about 30 pictures a second, nothing wrong in them.
No phone was at hand this night: what was found, from the code, the PC and the
emulator.

What the phone did that the PC does not:

- **The VU's arithmetic one lane at a time.** `PS2RT_VU_SSE` was keyed on
  `__SSE2__`, which Arm's compilers do not define: every FMAC of VU1's
  microprograms (the VIF1 worker, the heaviest thread on the PC too) and of
  the EE's macro mode ran the model's scalar path in doubles. Now the SSE code
  through sse2neon - checked against the model on arm64 under qemu (5.2 M
  FMAC lanes, 7.5 M MMI results, none differ). Two traps: sse2neon's
  `_mm_setcsr` sets the rounding only (no flush to zero: the clamp-free DAZ
  path would have been wrong), so the FPU control is FPCR written directly;
  NEON's float-to-int saturates both ways itself (FTOI without the xor).
- **Its compiled VU blocks returned to the loop after each** (musttail was
  x86-64's only); the EE's MMI lanes and the GS's 16-bit stores were scalar.
- **The library built as any shared library**: every call between the game's
  9,300 functions through the PLT, every global through the GOT, stack
  canaries in each frame, no LTO, no PGO. Now hidden symbols, no PLT, no
  canaries, ThinLTO, and PGO from counts made on the emulator (the same
  sources: `android.py pgo`).
- **MODERN graphics as on a desktop GPU**: LOD HIGH and PARTICLES MORE are
  more VU1 and GS work. A phone's MODERN is the integrated GPU's lighter one,
  with the game's own LOD and particles.
- **The scheduler's choice of cores.** The heavy threads (EE, GS, VIF1, the
  renderer) are now on the big cores (those within 3/4 of the fastest's
  clock) and in an ADPF hint session told the busiest one's work each frame,
  so the clocks rise with the work instead of after it; the blank timer no
  longer spins.

Still to measure on the phone - PORT SETTINGS > VIDEO > PERFORMANCE OVERLAY at
FPS + CPU shows each heavy thread's share of a core and the GPU's busy share:

- **The GPU's render passes.** A race frame on the PC: ~190 passes, ~73
  copies of a target the next draw reads ("own copy" ends), ~200 barriers,
  ~90 compute dispatches. A desktop GPU does not mind; a tiler (Mali) loads
  and stores the colour and the depth-stencil (D32S8: two planes) at each
  pass's start and end - at 640x448 about 5 MB a pass, hundreds of MB a frame
  if most are the frame's own target, near a phone's whole bandwidth at 60.
  Done the same night, verified exact on the PC: **deferred passes** (on by
  default on Android; `PS2HOST_GPU_DEFER_PASSES=1` elsewhere) - a pass's
  draws recorded into a secondary command buffer, the pass begun as it ends
  with its render area the draws' own and the depth-stencil loaded and
  stored only where a draw read or wrote it (`LOAD_OP_NONE` through
  VK_EXT/KHR_load_store_op_none). The race: 86% of the targets' area, the
  depth-stencil neither loaded nor stored in ~29% of the passes. Then the
  work between draws kept beside the open pass rather than ending it - GS
  memory uploads (no draw reads local memory) and texture decodes (renamed
  into another image where the pass already sampled the texture): render
  passes in the race 1,042,857 -> 611,574 (-41%), all exact.
  If GPU still shows near 100%, the levers next: the own-copy reads done inside
  the pass (VK_EXT_rasterization_order_attachment_access /
  dynamic_rendering_local_read where the driver has them), texture decodes
  moved out of passes, barriers with exact stages instead of
  ALL_COMMANDS (a tiler drains its vertex/fragment overlap at each).

## Weak machines, with the GPUs down (2026-10-29/30, night)

Goal: a high frame rate held on weak machines. Both of this laptop's GPUs
were stopped by Windows all night (Device Manager code 43; Vulkan saw only
Microsoft's CPU device, Vulkan 1.2) - no real-time runs, no GPU
regressions, no profile-guided training. So the night measured and changed
CPU work alone, each step checked without a GPU: the VU benches' hashes,
host tests (each new one shown to fail on a broken change), the software
GS's car_select and race_training, and the GS replay's pictures.

**Measuring the GS thread without a GPU of one's own** (engine c5b95af):
Chrome's SwiftShader is a Vulkan 1.3 device (`VK_DRIVER_FILES` = its
`vk_swiftshader_icd.json`, copied out of Chrome's folder - an update moves
it). The pile-up's replay with `PS2HOST_GPU_NO_DRAW=1` (a measure: the
primitives not drawn) and `PS2HOST_SPIN=0`; the replay now prints each
pass's cycles (QueryThreadCycleTime): the GS thread's least of 20 passes
repeats within 1% (~50 M cycles, ~17.5 ms - what the RTX 4070 measured).
The renderer thread's number there is SwiftShader's own command recording:
not a measure of ours. Pictures drawn on SwiftShader compare byte for byte
between two builds.

**Done** (engine, LOCAL):

| | change | measured |
| --- | --- | --- |
| VU1 | CLIP without branches, its bits put together in the vector (da8aa95, a2929f7) | fast VU bench -5 to -7% (crash, pile-up, race), exact |
| VIF | sub-word UNPACK formats (V3-16, S-16, V3-8, V2-8, S-8, V4-5) in UnpackRun's one loop, not a byte vector a word (db9cb53, b73ff30) | not measurable without the real-time run; host test against Word's byte buffer, in runs and split |
| GS | CSM1 CLUT loads from a table of offsets, not 256 Memory::Read (e935646) | GS thread -3.7% on the pile-up's replay; pictures the same |

**Tried, dropped:** VU stalls settled by the generator (~1%, mixed), the
run's state as the blocks' arguments (+2.5%), DIV/SQRT results uncleaned
(no change), OnDraw's slow path in a function of its own (+3%: the fast
path's own vector work keeps its frame), a CLUT load the model made
itself left out of the bridge's mutations when its entries and rows were
as before (GS thread the same: 47.1 against 47.2 M cycles). Details in engine
docs/VU_RECOMPILER.md.

**Burn3** (github.com/UwUCammie/Burn3, read 2026-10-29): despite "no EE or
VU emulation", an EE interpreter with translated blocks and VU emulation,
with hand-written C++ for chosen game functions and for the car and strip
VU1 programs (recognised by their microcode, a verify mode running the real
VU1 beside them and comparing every word) and "mesh tokens" (a mesh sent
once, a transform a frame; matched within 1/8 pixel - not exact). MIT,
its renderer LGPL; it does not build from a clone. Its idea worth taking is
the native VU1 program with a verify mode; here that would be program
2678EB26 (entry 1548: 14-21% of VU1 in every scene) - a dispatcher of 83
pairs into bodies of up to 435 pairs and ten loops, its results to be the
compiled code's to the bit (registers left included).

**Where it stands.** The CPU's levers measurable without a GPU are spent
to a percent or two each: the VU1 code runs near four instructions a cycle,
its clamps three times its arithmetic, VF held in host registers within a
block worth ~4% (VU_RECOMPILER.md); the GS thread's remaining parts are
the bridge's bookkeeping, which needs the GPU regressions to change. With
the GPUs back: GPU regressions (car_select, race_training, race) on
tonight's build, then lowbench (2 cores without HT, the crash's pile-up,
the Intel GPU) with `BIN=` b3_night_before_np.exe and b3_night_after_np.exe
(build/port/cmake, both without the profile), then the profile refreshed.
The profile-guided build of tonight's code (the old profile) on the VU
benches against the night's first: crash 40.5 -> 39.5 ms, pile-up 41.4 ->
37.8, race 35.8 -> 35.1; the pile-up's GS replay ~43.5 M cycles a pass.

**With the GPUs back (2026-10-30, after a reboot).** GPU regressions on
tonight's build: car_select 44/44, race_training 55/55, race 67/67. The
crash's pile-up on two cores without hyperthreading (`PS2HOST_CORES=0x5`,
the Intel GPU, the PS2's resolution, real time from 12,500, frames
12,900-14,400), both builds without the profile, run alternately (the
first run after the reboot, 3.8 s held on a cold pipeline cache, left out):

| | dropped | late in 1,500 | VIF1 worker busy | GS thread busy |
|---|---|---|---|---|
| before the night | 2, 0 | 12, 4 | 40.4%, 36.0% | 37.0%, 39.4% |
| after | 0, 0, 0 | 4, 7, 3 | 37.2%, 33.4%, 33.8% | 36.2%, 35.8%, 36.8% |

About 9% off the VIF1 worker (VU1 and the unpacking) and 5% off the GS
thread, and fewer late pictures.

**The dropped tries, added up.** Each was dropped because it measured no
change or worse, so together they add nothing: the ones that measured
nothing stay nothing, the ones that measured worse (+2.5%, +3%, the
clamp-free and clean-edge versions +5-12%) would subtract. The one mixed
(stalls settled by the generator: pile-up -3%, race +1-4%) nets about
zero. The unbuilt estimates are the headroom: VF in host registers ~4%,
steady-state loops ~3-5%, SIMD across iterations ~5-7% of VU1 - they cut
the same loads and checks, so perhaps 10-12% of VU1 together, a few
percent of a two-core machine's frame.

**Two cores, after the refreshed profile** (same bench, two runs each):
the default 5 and 1 late, VIF1 31.6-33.0%, GS 31.6-35.0%;
`PS2HOST_GPU_THREAD=0` 5 and 4 late, the GS thread 49%; `--vif1-sync` 9
and 5 late. D's thread moves are no better than the threads as they are;
at 60 two cores without hyperthreading are done (0 dropped, 1-5 late in
1,500, ~1.1 cores used).

**120 a second on two cores** (`PS2HOST_FRAME_RATE=120`, real time from
10,600 so the judgement sees play): 17-46 of 240 late, stepped down to
60 each time. In the 120 stretch the CPU: EE 36-41%, GS 33-41%, VIF1's
worker 36-40%, the renderer 16-18% (~1.4 cores, with the driver's on top),
the Intel GPU 61-64% busy; the GS thread is busy 65-79% of the wall time
on 33-41% of a core - waiting for a core, not for work. Every picture at
120 is the whole picture again (VU1's transforms, the GS's primitives):
what would hold 120 here is less per picture, not other threads.

**Open: a 4 s stall, 2 runs in 12** (before1 after the reboot, frame
~13,830; hf120_1, frame ~13,970 - the same moment of the crash): the GS
thread "busy" 4.0 s with ~27 ms of it in its own spans, VIF1's worker and
the EE waiting for it, the process at 1.8 cores - Windows' ~4 s lift of a
starved thread is the likeliest end of it. Not the pipelines (56 made, the
longest 6.4 ms, `PS2HOST_PIPELINE_LOG`), not the 4,096-texture count
(every run reaches it). Normally the spinning waits cost nothing (VIF1's
worker 0.06 s, the EE 0.22 s waiting in 25 s), so they were left as they
are; catching it needs the renderer thread's work on the timeline or a
stack taken when the GS thread is busy over a second.

**Option 1, native VU1 code (Burn3's idea), measured (2026-10-30).** The
hottest loop by pairs of 2678EB26:1548 (0x08E0, the strip vertices) written
natively and called from the compiled block: exact (every snapshot's hash),
but no faster - the compiled loop already runs at ~0.26 ns a pair, a quarter
of the program's average, and is ~4% of the VU work, not the 6% its pairs
suggested. The program's time is in branchy code spread over dozens of
blocks. Dropped; details in engine docs/VU_RECOMPILER.md.

## Option 2, sketched: in-between pictures made from the GS stream (2026-10-30)

**Now.** At 120 each in-between picture is a whole picture again: the game
draws with its camera and transforms placed between two steps (high_fps.cpp),
then VIF1, VU1, the GS thread and the renderer do all of it. Two cores at
120: EE 36-41%, VU1's worker 36-40%, GS 33-41%, renderer 16-18% - ~1.4
cores, and late pictures.

**The idea.** The game runs as at 60 (one draw a step). The GS thread's
input for each real picture - its feed ring's entries, after VU1: the GIF
packets of PATH1/2/3, the registers, the frame marks - is kept for the last
two steps. An in-between picture is made from those two, matched and
blended, and fed to the GS thread like a real one: no game draw, no VIF1, no
VU1 for it. The engine's display-list blending (display_list.h) does this
one stage earlier (VIF1 chains), which saves only the EE - not what is full.

**Matching.** Packet by packet in order: the same GIF tag (NLOOP, FLG,
registers) and the same non-vertex registers (TEX0, ALPHA, TEST, FRAME...)
are a pair; out of place, the nearest by first vertex (as display_list.cpp
does for moved segments). In a pair, XYZ (and Z), STQ/UV and fog blended;
colours, texture and state the newer's. Not matched (VU1's clipper gives
strips at a screen edge or the near plane another vertex count; sparks that
came or went): the newer's, unblended. A camera cut (Burnout's crash camera
shots, replays): the newer picture whole (the step's own rule now).

**What must not change.** Real pictures stay the game's own (at 120 the same
as at 60 - a new regression check: the real frames of a 120 run against the
60 recording). An in-between picture changes nothing the game can see: no
read-backs for the EE, no IMAGE transfers into GS memory it keeps, and the
persistent targets (the picture the speed blur reads back, the glow's)
saved before it and put back after (GPU copies), so the next real picture
starts where the last real one left off.

**Cost, expected.** An in-between picture: the match and blend (a pass over
~50-100 K words, ~1 ms) plus the GS thread and the renderer. Two cores at
120: about -0.25 cores (VU1's half of the worker, the game's extra draw) -
~1.15 cores, near what 60 takes now. A later stage could blend the
renderer's records instead and save the GS thread's half too (~-0.2 more),
at the cost of handling texture uploads and target state at that level.

**Latency.** A real picture must be through the GS before the in-between
before it can be made: on a weak machine the in-between is ready later than
now; shown at the same times, it may need one more picture of lead.

**Steps, each measured before the next.**
0. Offline, from two real pictures' GS dumps (crash, race, pile-up): how
   many packets match, how many words would blend, the matcher's time. Go
   on only if nearly everything that moves matches.
1. The feed recorded a step at a time, a blended in-between injected at 120
   (Burnout only, behind a switch), the game's own in-between draws off.
   Checked by eye on recorded runs (high_fps_trace.py, frame dumps) and by
   the 120 run's real frames against the 60 recording.
2. The persistent targets saved and restored around an in-between.
3. lowbench at 120 on two cores (the crash, the race), against now.

### Option 2, step 0: the packets match (2026-10-30)

Captured 16 pictures each (`PS2HOST_CAPTURE_FRAME=N PS2HOST_CAPTURE_FRAMES=16`,
virtual time, the GPU; replay.bin now has a 'V' at each frame's mark) of the
race (13,500), the crash's pile-up (13,080) and its explosion (14,560), and
lined each picture's GIF packets up with the last's by shape (path, FLG,
NLOOP, NREG, REGS, PRIM): `engine/tools/gsmatch.py <capture>/replay.bin`.
A picture: ~3,000 packets, 55-75 K vertices, 0.6-0.9 M words of PATH1 (VU1's
XGKICKs) and 0.5-0.7 M of PATH3 (textures, sent again every picture).

| | blendable (matched, only vertex values changed) | new | matched vertices moving < 64 px | >= 256 px |
|---|---|---|---|---|
| race | 96.8% of vertices | 3.2% | 94% | 3.4% |
| pile-up | 94.9% | 5.1% | 97% | 0.8% |
| explosion | 97.3% | 2.7% | 99% | 0.4% |

No matched packet with vertices changed its state (texture, blend: those
are packets of their own, without vertices). VU1's clipper costs 3-5% of the
vertices a picture (new, shown unblended); the few moves of 256 px or more
are the jumps the rule leaves unblended. Matching ~3,000 packets: 0.05 s in
Python, well under a millisecond in C++. Go on to step 1. For it: the
textures are uploaded again every picture - an in-between picture should
leave out those the same as the last picture's, which holds only where a
picture does not reuse texture memory within itself (to check).

### Option 2, step 1a: pictures made between, from a replay (2026-10-30)

`engine/host/src/gs_blend.cpp` (ps2host/gs_blend.h): each path's words cut
into GIF packets, the two pictures' packets lined up by shape with patience
alignment (equal ends, shapes found once in each as anchors, the longest run
of them in order, the gaps the same way, a small gap without anchors by the
longest common subsequence - as difflib's to a few vertices; a nearest-way-
back walk lost a third), matched PACKED packets' XYZ (ADC kept), Z, ST/Q, UV,
RGBA and FOG blended, a packet moving over 256 pixels the newer's.
`PS2HOST_REPLAY_GPU=1 PS2HOST_REPLAY_BLEND= <exe> --gs-replay replay.bin out`:
a multi-picture capture replayed with a picture made between each two.

| | blended | not matched | in packets that jump | the blend's time |
|---|---|---|---|---|
| race | 86% of vertices | 3.6% | 10.1% | ~2.2 ms |
| pile-up | 89% | 8.6% | 2.7% | ~2.2 ms |
| explosion | 96% | 2.7% | 1.7% | ~2.2 ms |

The time: copying the newer picture ~1.0 ms (textures and all - the live
one need not), lining up ~0.5, blending ~0.7 (integers in 256ths: with a
double's rounding each it was 4-5 ms). By eye (a plain replay's real
pictures either side of the replay's made one): the cars, the dust, a fence
post passing - each halfway; no torn or thrown triangles; the HUD whole.
The real pictures replayed after a made one differ from a plain replay's (10-18 levels
on average): the speed blur reads the picture before, which was the made
one - step 2 (the targets put back) is needed, as sketched. Open: the race's
jumped packets (10% of its vertices: some vertex moving 256 pixels or more -
off-screen ones in the guard band, likely) are shown as the newer's.

### Option 2, step 1a corrected (2026-10-30, later)

**The pictures above were the wrong ones.** Burnout draws a frame into 0x46
and copies it to the displayed buffer (0) in the next blank - at the start
of the next frame's GS input - so a replay's capture after a stream shows
the drawing of the stream before it. In a blended replay the capture after a
made picture's stream is the real picture before it (0.05-0.33 levels from
the plain replay's), and the made picture is the capture after the next real
stream. So: the real pictures are not changed by the made ones (0.04-0.32,
one picture 1.06) - step 2 (targets put back) may not be needed; and the
made pictures, looked at rightly (plain k, made, plain k+1), are between
their neighbours but not clean:
- the race: right (the fence post halfway), a small sliver at the bottom.
- the pile-up: a bright vertical strip where the white car comes into the
  picture from the bottom left (found in pass 127 of the made stream with
  `PS2HOST_GPU_DUMP_FRAME`: a strip textured with the HUD's "BR" letters,
  stretched up the screen) - present with every kind of packet hidden in
  turn (`PS2HOST_BLEND_HIDE`: 1 not matched, 2 jumped, 4 blended, 8 still),
  so not simply a pairing of two things; not yet understood.
Tried for it, no good: blending only under a state packet that matched and
was the same (the race and the explosion lost 36-50% of their vertices:
their state changes picture to picture), and a jump relative to a packet's
size (16 pixels and half its side: 73-78% blended, the strip still there).
Kept: a vertex packet whose A+D or PRIM among its vertices differs is the
newer's. Blended 86% / 89% / 96% of vertices (race, pile-up, explosion),
~2 ms. Next: the strip (which packets draw it - a dump of the made stream's
primitives over that rectangle), then the live picture (1b).

### Option 2: the strip found and gone (2026-10-30, later)

Found with `PS2HOST_GPU_DUMP_FRAME` (the made stream's passes against the
real one's: first in pass 127), the GS trace (no full-screen column sprite
differs) and `PS2HOST_BLEND_HIDE` looked at closely (zoomed): a piece of the
road with no match, cut anew by VU1's clipper at the screen's left edge,
drawn where it was in the newer picture while the road round it was
blended - its texture (the crossing's stripes) out of line. Also behind it:
large polygons near the camera counted as jumps by off-screen corners
swinging thousands of pixels. Three changes in gs_blend:
- only a vertex on the screen (64 pixels round, from XYOFFSET) can jump;
- a packet with no match is *carried*: moved back `t` of the way along the
  affine map (least squares, newer to older) of the nearest blended packet
  within four in the list - mostly the same mesh's next piece;
- a matched packet whose vertices' moves are not one affine motion (to 4
  pixels or a quarter of the mean move) is *loose* - the clipper cut it at
  other points with as many vertices, its texture coordinates blended across
  the texture - and is carried the same way.
Blended: race 82-86% of vertices plus ~150 packets carried, pile-up 93-94%
plus ~125, explosion 94% plus ~87. The pile-up's strip is gone (picture 8);
picture 12 keeps a small bright patch above the white car coming in, thin
seams remain here and there; the race is right. The real pictures still as
a plain replay's (0.05-1.7 levels). The time grew to 3.2-4.5 ms a picture
(the motion test's vectors a packet: to be made cheaper). Next: the bright
patch, the seams, the time back to ~1-2 ms; then the live picture (1b).

### Option 2: the patch and the edges (2026-10-30, later)

A measure first: `artifacts.py` (session scratchpad) counts the pixels of
each made picture more than 40 levels outside the envelope of its two
neighbours (their min and max over 9x9) - what a picture between them should
not have. Before this round: race 0.35%, pile-up 1.35%, explosion 0.29% of
the made pictures' pixels. It favours stepping (the newer picture is always
inside), so the share of vertices left unblended is kept beside it.

What was found, each by pass dumps, the trace and PS2HOST_BLEND_HIDE:
- a carried packet moved by a neighbour that was another thing: carried only
  by a neighbour under the same state, the nearest on the screen within
  eight, and only where it lies within that one's box widened by its own
  size (an affine map taken past its points threw pavement over the kerb).
  Race 0.35 -> 0.044%, explosion 0.29 -> 0.11%, pile-up 1.35 -> 0.92%.
- the older's packets with no match added, moved forward (for the edges):
  no better by the measure, kept (`PS2HOST_BLEND_HIDE=32` leaves them out).
- the patch and the white blocks at the left edge: the white car coming in
  fast at the corner, its pieces blended, carried, loose and new together -
  torn over the road (pass 128 of the made stream). A thing (a run of vertex
  packets under one state) with under 70% of its vertices blended or still
  is now the newer's whole (`PS2HOST_BLEND_SNAP`, percent):

| need | race: strays (stepped) | pile-up | explosion |
|---|---|---|---|
| 0 | 0.052% (0%) | 0.97% (0%) | 0.11% (0%) |
| 70 (kept) | 0.043% (8%) | 0.38% (10%) | 0.049% (1%) |
| 90 | 0.004% (41%) | 0.27% (50%) | 0.043% (9%) |

Left: in the pile-up, long thin lines across the screen by the truck (its
skid marks or tyre trail - strips that move their vertices along, or
triangles whose far corners off the screen swing), and the cost (3.4-6.6 ms
a picture now). Then the live picture (1b).

### Option 2: the thin lines (2026-10-30, later)

The lines across the pile-up's road (picture 9, the made stream's trace:
three triangles from x -1500 to +490 that the real stream does not have)
were strips reaching far off the screen on both sides (x -1710 and +2356,
a road's line) whose far ends swapped places between the pictures: off the
screen in both, they passed the jump test (the screen only) and the motion
test (the vertices on it only); blended, a corner crossed the screen.
- A vertex off the screen in both pictures but on its other side, in front
  of the camera both times (Q > 0: behind it, the projection itself throws a
  vertex over - the road under the car), keeps the newer's place; the rest
  of its packet blends. (As a jump of the whole packet instead: the race
  stepped 16-17% of its vertices, not 8%.)
- Vertices with their Q (ST's third, 1/w) blended in perspective: (x -
  centre) * w with w, then divided; S and T as the vertex's s = S/Q on the
  blended Q; Z along Q. Corners near the camera swing on the screen by
  hundreds of pixels as w nears 0 - linear on the screen they were wrong.
- Tried, dropped: an off-screen vertex jumping past half (or all) of its
  way from the screen - the race stepped 18-19%.

| | blended | stepped | strays |
|---|---|---|---|
| race | 84% | 8% | 0.021% (from 0.043) |
| pile-up | 84% | 10% | 0.30% (from 0.38; picture 9 4,840 -> 1,697) |
| explosion | 95% | 1% | 0.049% |

What the measure still marks is mostly the edges of fast-moving things
(the camera swings 30+ pixels a picture: past the 9x9 envelope) - the
pile-up's picture 13 marks a lamp post's and buildings' outlines and is
clean to the eye; left by eye: faint hairlines across the road by the
truck, a traffic light's glow brighter. Next: the cost (3.4-6.6 ms a
picture), then the live picture (1b).

### Option 2: the cost (2026-10-30, later)

Timed by part (the replay's line: copy, align, blend - packets, snap,
carry, add): the pile-up 7-8 ms a picture, of it the older's unmatched
packets added 2.2 ms and the packets' blend 3-4 ms. The adding is off now
(`PS2HOST_BLEND_ADD=1` on): with it off the strays are *fewer* (pile-up
0.30 -> 0.21%) - it drew things twice. The perspective blend divided per
vertex where a multiply by the blended Q does, and rounded with lround:
packets 3-4 -> 1.4-1.8 ms. No vector made a packet (kept, thread_local).

| | strays | blended / stepped | a picture (of it the copy) |
|---|---|---|---|
| race | 0.013% | 84% / 8% | 2.6 ms (0.7) |
| pile-up | 0.21% | 84% / 10% | 3.3-3.7 ms (1.0) |
| explosion | 0.048% | 95% / 1% | 2.9 ms (0.9) |

The copy is the replay's (the newer picture whole, textures and all); the
live one copies the vertex packets only. Next: the live picture (1b).

### Option 2, step 1b: live, and measured against the 120 we have (2026-10-30)

Built (engine, behind `PS2HOST_GS_BETWEEN=1`; off, nothing changes - GPU
regressions car_select 44/44, race_training as recorded): each frame's GS
input kept (Gs::KeepInput); at its end the frame's copy to the shown buffer
(the input up to its first FRAME set elsewhere: GsFrameCut) played and the
picture held (Display::HoldImage), the picture between made, its known
image transfers left out (DropKnownUploads: ~85% of ~590 a picture - ~15%
go to texture memory used again within the picture and stay), played, the
copy played again (the game's own trail blend), presented half a frame
before the held one; the GS's registers put back (Gs::Save/Restore). The
game stays at 60; 119.9 presents a second on all cores, 14 late in 3,600.

But on two cores it loses to the 120 we have (the game drawing each picture
between itself), the crash's pile-up, two runs each:

| two cores | pictures between from the GS input | the game's own 120 |
|---|---|---|
| Intel UHD | ~97 presents/s, 173 dropped, 179 late; GS thread 100% busy | 120/s, 12 dropped, 45 late; GS 59% |
| RTX 4070 | ~104/s, 62 dropped, 275 late, 418 blanks lost; GS 100% | 120/s, 0 dropped, 6 late; GS 43% |

Why: a picture between costs the GS thread about what a real one does (its
decoding and the bridge, plus the ~2-3 ms blend) and all of it lands on the
GS thread, the one already fullest; the game's own 120 spreads its extra
picture over the EE, VIF1's worker and the GS thread. And VIF1's worker
spins (yield) while the GS thread is full: 53-62% of a core on two cores,
taken from the GS thread. The saving it was for (no VU1, no EE draw for the
picture between) is real (~0.3 cores on all cores) but on two cores the GS
thread is the wall.

What would make it pay: the picture between made after the bridge - the
renderer's records blended, no GIF decoding and no bridge for it (the
sketch's later stage) - or on a thread of its own. Neither is small. Kept
as built, off. The cheap lever found on the way: VIF1's worker's spinning
wait for room in the GS ring (both modes, on two cores).

### The GS ring's waits asleep (2026-10-30)

A producer waiting for room in the GS thread's ring (VIF1's worker, the EE)
or for it to empty (Sync) spun the whole wait (`yield`); now it spins a
moment (256 pauses, 16 yields), then sleeps until the GS thread, taking
entries, wakes it (`Feed::Await`/`Room`; 1 ms at most asleep). GPU
regressions car_select, race_training and race as recorded.

Two cores (0x5), the Intel UHD, the crash's pile-up, the builds alternated:

| | before | after |
|---|---|---|
| 120 (the game's own) | 0 dropped, 3 / 0 late, 1 / 0 step-downs | 0 dropped, 2 / 1 late, 0 step-downs |
| 60 | 0 dropped, 1 / 3 late | 0 dropped, 1 / 0 late |
| pictures between (option 2, GS thread full) | 99 presents/s, 222 late; VIF1's worker 51% CPU | 98/s, 269 late; VIF1's worker 17% CPU |

Where the GS thread is not full it changes nothing (the worker seldom
waits); where it is, a third of a core of spinning is gone - but the GS
thread, busy 100% of the time on ~64% of a core, was not short of CPU:
its waits are the renderer's and the GPU's. Kept (less heat and power on a
laptop, the core free for anything else).

After the reboot the two-core machine holds the game's own 120 on the Intel
UHD (0-3 late in 1,500, no step-down) - the 45 late and the step-downs
measured before it (and the Intel driver's crashes at the window's start)
were the machine's trouble that day, not the port.

### Two cores at 120, more scenes (2026-10-30)

Two cores without hyperthreading (0x5), the Intel UHD, the PS2's resolution,
the game's own 120 (`PS2HOST_FRAME_RATE=120`), two runs each (late/dropped
in the measured stretch; median and 99% interval):

| scene | run 1 | run 2 |
|---|---|---|
| race start (race, 11,600-12,800) | 4 late, 0 dropped; 8.38 / 11.09 ms | 1 late, 0 dropped; 8.30 / 8.97 |
| mid-race (12,600-15,300) | 18 late, 0 dropped; 8.34 / 11.05 | 10 late, 0 dropped; 8.33 / 9.13 |
| the crash's explosion (14,300-15,400) | 0 late; 8.33 / 8.82 | 2 late; 8.30 / 8.98 |
| the crash's pile-up (above) | 0-3 late | |
| splitscreen (9,900-12,900) | stepped down: 63 of 240 late at 120; then 60, 40 late, 4 dropped | stepped down (226 of 240 late); 60, 42 late, 40 dropped |

(The explosion's one step-down each run came before its real time, in the
virtual-time lead-in, and went back up.) One player holds 120 on two cores
and the integrated GPU. Splitscreen does not: two views are twice the
drawing - at 120 the GS thread 91% and VIF1's worker 76% busy - and even at
60 two cores are at their edge there (40 dropped in one run). Splitscreen on
two cores is the weak machine's limit now.
