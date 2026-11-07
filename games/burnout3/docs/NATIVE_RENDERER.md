# The native renderer: RenderWare's VU1 programs drawn by the GPU

Branch `native-renderer` (engine `b3-native`, from the engine's `native-renderer`: the framework RE Outbreak's
port built - engine/docs/NATIVE_RENDERER.md, level B). Started 2026-11-07.

Burnout 3's 3D is drawn by RenderWare's VU1 microprograms: the game hands VU1 a batch of vertices and a few
constants, the microprogram transforms, lights, culls and clips them, and kicks GS packets. Emulating that is
the VIF1 worker's main cost (the race at 120: 42% of a core). A native program does a run's work without the
microcode: the CPU decides what the microcode would (its path, which triangles, what it leaves in VU1's memory
and registers), and the GPU draws the batch from the game's own vertices with a vertex shader that computes each
vertex exactly as the microcode does (rounded toward zero, the VU's arithmetic). Nothing changes in the
pictures: every scripted run is as recorded.

## The programs (`port/vu_native_rw.cpp`; engine `gpu/shaders/gs_mesh_rw.vert`, `host/include/ps2host/rw_native.h`)

| VU1 image | draws | share of the race's VU1 | native |
| --- | --- | --- | --- |
| 7E86891B | the cars: a quadratic form's lighting, a Fresnel alpha, the sphere-mapped env pass (two draws a batch), the guard band's ADC; the skinning (crashed cars: two bones a vertex) | 36% | all |
| D4B61373, 34C3CF67 | trackside props: one batch drawn once an instance (its matrix, colour or light) | 12% | all but C with B (never seen) |
| 2678EB26, B9F6CB25 | the world's strips: the screen winding's cull, path B's guard band, path C's fog and six-plane clipper | 15% | all |
| 1332C99F | lit strips (car bodies, the garage's): 2678's paths with a specular alpha | 11% | all |
| 01CE30D5 | prelit strips (track, scenery): paths A, B, C | 8% | all |
| F504D280 | the sky dome (generated, its own clipper) | 13% | no: the microcode |
| 0835A68B | 01CE's mirrored (reflection) pass | 3% | all (the mirror's prologue on the CPU) |

Each program is registered by a hash of its code (`VuNativeHash` over its instruction memory range), so the
images that share code (B9F6 with 2678, 34C3 with D4B6) are covered too.

How each was specified: a `--vu-snapshot` of 6,000 race runs; `build/native/spec_<image>.md` (each verified by a
Python model of the engine's VU against every snapshot run, word for word); then a C++ twin (`rw_native.h`), a
check against the microcode in the game, and the shader.

**The GPU's share.** Meshes (the batches' vertices as VIF1 left them) are uploaded once, keyed by their hash;
each run sends a small header (its matrices and constants) and its triangles' indices. The world's strips are
drawn with corner indices (3t + c): each corner decides its strip triangle as the microcode's ADC does (the
winding, the guard band) and a triangle not drawn collapses to a point - no CPU work a vertex.

**The CPU's share.** What the GPU cannot: the path, the guard band's CLIP flags where the next run or its packets
depend on them, the world's path C - its first pass (every vertex's place and judgement, the batch's cull), the
CLIP register's window the clipper's choices read, the clipped polygons themselves (kicked as GS packets
between the native draws, in the microcode's order), and what a run leaves in VU1's memory (the batch
transformed in place, the tags, 1332's alphas) and registers (the transform, CLIP, the buffers' parity).

## Checking

- `PS2HOST_NATIVE_RW=check`: every run is the microcode's; the twin's packets are compared word for word with
  what it kicked (path C: its triangles in order), and CLIP, the registers and the kicks' count with what it left.
  The race: 3.0M runs, 1.52G words; the crash run: 4.36M runs, 2.39G words; the garage: 0.54M runs - 0 differ.
- Every scripted run as recorded with the native programs on (race, crash, car_select, road_rage, eliminator,
  race_training, race_mods, mods_garage, memcard_save; splitscreen's two menu pictures differ with the build
  before this work too).
- `PS2HOST_NATIVE_RW=0` off; `PS2HOST_NATIVE_RW_STATS=1` why runs were left to the microcode;
  `PS2HOST_NATIVE_FALLBACK=1` every native draw from the CPU twin (the renderer's mesh path bypassed);
  `PS2HOST_NATIVE_RW_PATHC=0|fast`, `PS2HOST_RW_TEST_IMAGE=<0-2>`, `PS2HOST_RW_TEST_SHADOW=1` (path C drawn
  natively, VU1 left by the microcode run silently), `PS2HOST_RW_GPU_ALPHA=1` (1332's alpha in the shader).

Found on the way (each a picture or a word that differed):

- Pipelined loops judge vertices past the batch (the instanced strips: three; 2678's path B and path C's tail:
  one) - from memory as the run has written it by then (an output buffer's tag, a flushed packet's count).
- A transform with no w (all zero: an object with no constants) saturates on the VU: left to the microcode.
- 1332's alpha in the shader differed from the VU's in a few vertices; made on the CPU it is the microcode's
  pre-pass, written into the batch as the pre-pass writes it.
- Path C's polygons split into packets only between polygons (a packet cut after a polygon's first vertex
  drew a sliver).
- The renderer's header range: the characters' 17 KB a draw filled the vertex ring (the GS thread waited on the
  GPU); RenderWare's use the room's 800 bytes, a skinned batch's palette the large one.
- Allocations a run were a tenth of the VIF1 worker's time: the runs' state is reused.

## Measured (RTX 4070 Laptop; the race in real time from frame 12000, `build/native/rtbench.sh`)

| | VIF1 worker | GS thread |
| --- | --- | --- |
| 60, native off | 18.9% | 18.2% |
| 60, native on | 14.1% | 14.5% |
| 120 at 2x, native off | 42.4% | 46.1% |
| 120 at 2x, native on | 29.6% | 34.0% |

VU1's time in the scripted race (virtual time, `PS2HOST_VU_PROFILE`): 5.8 s -> 3.5 s; what is left is mostly the
sky dome and the native programs' CPU share. The VIF1 worker's samples at 120: VIF1's unpacking 6.7%, path C's
first pass 3.1%, the meshes' hash 3.1%, the rest under 2% each.

## Graphics

PC SETTINGS > VIDEO > MSAA (off, 2x, 4x, 8x): the engine's multisampled 3D (every draw's, native or not).

## Next

The sky dome (F504D280: generated geometry, its own clipper - under 2% of the VIF1 worker in real time, with
the other microcode left), the enhanced draws
(precise geometry, per-pixel reflections for the cars' env pass) on the GPU path the native programs opened.
