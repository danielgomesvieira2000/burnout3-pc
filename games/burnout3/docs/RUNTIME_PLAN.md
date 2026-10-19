# A lighter frame: native effects, a leaner renderer, VU1

Status: **A2 (fog) done** (engine 8edd32e, branch effects: a channel shuffle
on the target; see A2). Written 2026-10-19 from a profile of the
scripted race (`race.args`). The work isn't done yet. This sets out where a
race frame's time goes, why a 2004 game is not light on a 2020s PC, what
would make it light, and the order.

## Why it is not light

The game's own code is light: translated to native code it uses about a
third of a CPU core. The cost is the PS2's hardware, reproduced exactly:

- **VU1**, the vector unit that did all the 3D maths, runs the game's
  microprograms. A PC GPU cannot run them, so the port runs them on the CPU.
- **The GS**, the graphics chip, had very fast embedded memory. The game
  leans on it: glow, blur and fog are dozens of small passes over buffers
  that alias each other (the glow's strips live in the depth buffer's
  memory) and are read back as textures through 16-bit views and palettes.
  Kept exact, each pass, view and read-back is a Vulkan render pass, a
  barrier, a layout change, a copy or a swizzle.

## What the profile shows

i7-13620H, RTX 4070 Laptop and Intel UHD (the same laptop), the race from
frame 18,000.

**CPU per frame** (real time, all cores, RTX at 2x; the profiler's
timeline):

| Thread | Busy | Of which |
| --- | --- | --- |
| VIF1 worker | 7.6 ms | VU1 microprograms 7.1 ms |
| GS thread | 5.1 ms | recording Vulkan work ("GPU bookkeeping") 2.6 ms |
| EE (the game) | 6 ms | VU0 1.9 ms, SPU2 0.7, IOP 0.7, DMA/VIF 0.5 |

**GPU per frame, Intel UHD at 2x** (virtual time, `PS2HOST_GPU_PROFILE`,
frames 18,000-18,600, one effect off per run with `--setting
debug.switches_off=<the game's switch bytes>`):

| Run | GPU ms | Saved | Passes | Global barriers | Transitions | Feedback copies |
| --- | --- | --- | --- | --- | --- | --- |
| Base 2x | 20.8 | | 232 | 449 | 514 | 76 |
| Base 1x | 13.6 | 7.2 | 232 | 450 | 514 | 76 |
| Bloom off | 17.6 | 3.2 | 171 | 271 | 289 | 41 |
| Fog off | 17.4 | 3.4 | 229 | 435 | 498 | 74 |
| Blur off | 19.5 | 1.3 | | | | |
| Envmap off | 20.0 | 0.8 | | | | |
| Shadows off | 20.7 | 0.1 | | | | |
| All effects off | 12.8 | 8.0 | 50 | 170 | 96 | 3 |

(The profile writes a timestamp after every command, which costs a little
itself; the real-time runs below agree.)

The RTX at 2x: 6.3 ms, draws 62-66%, swizzle 18-20%, feedback copies 7-8%.

**Real time, Intel UHD, all cores** (`bench.sh`, no per-command profile):

| Run | New pictures/s | Late | GPU busy | GS thread |
| --- | --- | --- | --- | --- |
| 2x | ~58 | 460 of 2,800 (79 dropped) | 86% | 60-100% |
| 1x | 59.9 | 0 | 47% | 33% |
| 2x, effects off | 59.9 | 0 | 45% | 28% |

**The frame** (GS trace, frame 18,001):

- 44,381 primitives in 962 runs of the same state.
- The main frame (FBP 0x46) has 39,107 of them: 30,000 in one stretch, the
  rest interleaved with the envmap's three faces (0x18B, 0x193, 0x19B; the
  track draws each sector into the frame, then into the faces). That is 162
  target switches a frame.
- The post effects are about 40 primitives, each into its own small buffer:
  - the glow's strips into D2/D3 and EE (320x224);
  - 15E (160x112);
  - the blur's half frame at 0x16B;
  - the fog's 256 sprites through the depth buffer's 16-bit view into 0x169.

**What the renderer does per pass** (engine/gpu/src/gs_renderer.cpp):

- **Passes.** Dynamic rendering, always LOAD/STORE over the whole target. A
  pass breaks on any other target or depth, on a feedback copy, on a target
  turned into a texture, and on any non-draw command (decode, CLUT load,
  swizzle, upload).
- **Barriers.**
  - Every memory operation (swizzle, decode, CLUT, memory copy) is fenced by
    one or two global ALL_COMMANDS barriers, never merged.
  - Every layout change is a barrier of its own, always from ALL_COMMANDS.
  - synchronization2 is enabled but unused.
  - Two passes in a row over the same image in the same layout get no
    barrier at all. That works on today's drivers but is a hazard under the
    spec.
- **Feedback copies.** A draw that blends in the shader, or samples its own
  target, copies the region first: 4 layout changes and a pass break each.
  No dynamic_rendering_local_read, no rasterization-order access.
- **Depth.** A draw without depth gets a full-size dummy depth image (a pass
  break when ZTE toggles). A stale depth buffer always reloads whole, as a
  full-screen pass.
- **DATE.** A whole-target stencil pass per DATE batch.
- **CPU per primitive.**
  - About 4 copies of a 64-byte vertex, 6 vertices a sprite, non-indexed.
  - A 116-byte push constant every draw.
  - A heap vector per batch.
  - The GS thread drains the render thread once a frame.

## What this means

Three costs, in the order they matter for "light on any machine":

1. **The game's post effects, emulated literally.** About 40% of the Intel
   GPU's frame and most of the barriers, transitions, dispatches and copies,
   for a few hundred primitives:
   - **Bloom** is overhead: 60 passes, 180 barriers, 225 transitions and 35
     copies.
   - **Fog** is pixels: full-screen passes through a 16-bit view and a
     palette.

   With them off, the Intel UHD at 2x is lighter than at 1x with them on.
2. **The renderer's fixed cost per pass, barrier and copy**, paid by every
   game on every GPU. The Intel UHD is 13.6 ms at 1x against 20.8 at 2x (4x
   the pixels for 1.5x the time): most of its frame does not scale with
   pixels.
3. **VU1 on the CPU**, 7 ms a frame. It is the busiest thread on the RTX, and
   on a 2-core machine it is the first to run out.

## Phase A: the game's effects drawn natively (Burnout 3)

The same engine feature as GRAPHICS_PLAN's MODERN bloom and blur. It first
draws the ORIGINAL effects natively and only then the modern ones.

**A1. Engine: an effect at a point in the GS stream.**

- `ps2host::GpuEffect(id, params)` puts a marker into the GS stream after
  the draws before it.
- The GS thread, reaching it, runs an engine-built effect (compute or
  full-screen fragment) on the current frame target and its depth buffer,
  which already has a sampled view.
- The effect keeps its own images between frames (the glow's accumulator)
  and draws into the frame in one pass.
- Only on the GPU renderer: the software GS keeps the game's own draws.

**A2. Fog native. Done**, not as planned above: as a fast path in the
engine, for any game.

- **The game's packets can't be edited.** The fog function writes its GS
  packets into the scratchpad, which is sent out whenever it fills, so part
  is already gone by the time the function returns. A marker written from
  the EE (A1) would need the game's own packet writer.
- **What the fog does** (GS trace, the software GS's rules):
  - 40 sprites, 8 wide in a comb, draw into a 16-bit view of the picture
    with FBMSK 0x3FFF, sampling a 16-bit view of the depth buffer, 8 pixels
    to the left. Each 16-bit pixel is half of a 32-bit word, and the mask
    keeps each half's lower byte, so the picture's alpha becomes the depth's
    bits 8-15 and its colour is unchanged.
  - 256 points draw the palette into 0x169.
  - 20 sprites read the picture's alpha as an 8-bit index (PSMT8H) through
    it, MODULATE with the fog colour, blend (Cs - Cd) As + Cd, alpha kept,
    only where Z < 0xFFFF.
- **The fast path.** PCSX2 calls this trick a "channel shuffle". The bridge
  recognises it (`Shuffle`, gpu_bridge.cpp) and the renderer does it on the
  32-bit picture in one compute pass (`shuffle.comp`):
  - each pixel's halves are found in the 16-bit view, and their texels in
    the depth buffer, by the memory layouts' bit permutations inverted;
  - through FRAME's mask, at each sub-pixel;
  - a comb of sprites is one dispatch.
- **What it replaced:** the 16-bit image, the write-backs of it, of the
  picture and of the depth buffer, and the picture's reload, all at the
  internal resolution.
- **Exact.** Taken only where it is exactly the GS, checked once per
  sprite's place: every Burnout 3 GPU run as recorded, bit-identical at 1x.
  At 2x the alpha is per sub-pixel (about 1% of a frame's pixels differ, in
  the distance).
- **Measured, Intel UHD at 2x, GPU per frame:**

  | Run | Before | After |
  | --- | --- | --- |
  | Race | 21.8 ms | 19.4 ms |
  | Splitscreen | 29.6 ms | 26.7 ms |

- **Left of the fog's 3.4 ms:** the fog draw itself (the effect), the
  palette's points and the compute pass (about 0.9 ms).

**A3. Bloom native.**

- Override 0x002659C0: the bright pass, the 160x112 downsample and blur, the
  accumulator with its persistence, and the add-back.
- Done with the same arithmetic in 3-4 dispatches at the same buffer sizes,
  so the ORIGINAL look is kept, BLOOM HIGH and LOW included (its parameters
  are the view's, read as they are today).
- Expected: most of bloom's 3.2 ms, and about 180 of the 450 global
  barriers.

**A4. Radial and directional blur native.**

- Overrides 0x002FA9A0 and 0x00426C10: the 8 zoom taps and the mask in one
  pass, from the view's parameters.
- Expected: most of blur's 1.3 ms.

**A5. The MODERN effects (GRAPHICS_PLAN step 5)** on the same feature, at
internal resolution.

**Accuracy.**

- The native effects become the GPU's default only once their pictures match
  today's ORIGINAL within a small tolerance. They are compared frame by frame
  on `race.args` and `splitscreen.args`, with framediff.py, against the
  current GPU recordings.
- The exact path stays one setting away (`effects=exact`) for comparisons
  and the regressions.
- The GPU recordings of runs with effects are re-recorded once, with the
  tolerance stated.

**Watch: aliasing.** The game's effect buffers alias depth memory (D2/D3/E0)
and the envmap's pages. When the native effect skips the game's draws,
nothing may later read what those draws would have written. Check with a GS
trace that nothing does, other than the effect itself (the glow's
accumulator: kept in the effect's own image).

**Expected for Phase A:**

- Intel UHD at 2x from ~21 ms to ~13-14 ms: the effects-off 12.8 plus about
  a millisecond for the native passes.
- Single player at 2x on the Intel UHD then has headroom, and splitscreen at
  2x is within reach.

## Phase B: the renderer's fixed cost (the engine, every game)

Each step keeps every game's pictures bit-identical (they change
synchronisation and bookkeeping, not what is drawn). The gate is all
games' GPU regressions as recorded.

**B1. Precise barriers.**

- synchronization2 (`vkCmdPipelineBarrier2`) with the stages and accesses
  each operation needs, not ALL_COMMANDS.
- Adjacent barriers merged.
- A pending-hazard flag, so a barrier is recorded only before something
  that reads what was written.
- The missing pass-to-pass barrier on the same image added: required before
  any pass is reordered.

**B2. Fewer layout changes.**

- Colour targets kept in GENERAL (they already have storage usage), so a
  swizzle, a sample or a draw needs no transition, only the barrier.
- Or VK_EXT_attachment_feedback_loop_layout where the GPU has it.

**B3. Feedback without copies.**

- Shader blending (16-bit targets, Ad on sprites, FIX > 0x80) reads its own
  pixel through VK_KHR_dynamic_rendering_local_read (input attachments in
  dynamic rendering), or rasterization-order attachment access where present.
- The copy stays as the fallback, and for draws that sample other texels of
  their own target (the blur's offsets).

**B4. No dummy depth.** VK_EXT_dynamic_rendering_unused_attachments, or keep
the open depth attached with its test off: no pass break when ZTE toggles.

**B5. DATE within its scissor; depth reloads by rows,** as colour targets
already are.

**B6. CPU per primitive.**

- A 32-byte vertex.
- Indexed sprites (4 vertices, not 6).
- One copy from the bridge to the ring.
- Push constants only when changed.
- No heap allocation per batch.
- No per-frame drain of the render thread.

**B7. Later: target switches.** Record the envmap faces' draws in lists of
their own and replay each face's in one pass. This is allowed only where the
GS stream has no dependency between them: the faces are not read until the
warp. It cuts about 140 pass breaks a frame. Measure B1-B6 first: on the
GPU, the envmap's switches cost only 0.8 ms in all.

**Expected:** most of the global barriers and transitions gone, fewer
passes, the GS thread's 2.6 ms of bookkeeping lower. The size of the gain on
the Intel UHD's fixed cost is the open question this phase answers.

## Phase C: VU1 (CPU, 7 ms a frame)

1. **Profile which microprograms run the most** (the VU recompiler knows
   each program; count cycles per program per frame).
2. **For the hot ones,** in the order the profile gives:
   - **Native transforms.** The program's maths as C++ over 4-8 vertices at
     once (SSE/AVX). Checked against the exact VU by the existing VU hash
     comparison (vu-snapshot / vurecomp), so the output stays bit-identical.
   - **Splitscreen's two views on two threads.** Their VU1 lists are
     independent within a frame.
   - **On the GPU (compute)** only if the native versions are not enough. It
     is a large step: the output must come back in GS order.

Expected: hard to say before step 1. The programs are few and regular
(transform, clip, light), so a large share is plausible.

## Order

1. **A1 + A2 (fog).** The biggest pixel cost, and the simplest effect: proves
   the engine feature.
2. **A3 (bloom) and A4 (blurs).** Benchmark: Intel UHD at 2x single player,
   then splitscreen.
3. **B1-B2** (barriers, layouts): bit-identical, every game.
4. **B3-B6.**
5. **C1** (the VU1 profile), then C2's native transforms for the top
   programs.
6. **A5** (MODERN effects) and B7 as the measurements say.

## How it is measured

- `prof/matrix.sh` (the scratchpad's; to keep in tools): one GPU, the race
  to frame 18,700 in virtual time, `PS2HOST_GPU_PROFILE` and
  `PS2HOST_GPU_COUNT`, one switch off per run. GPU ms a frame, passes,
  barriers, transitions, copies.
- `bench.sh` (real time): new pictures a second (VIF1 transfers, not
  presents), late and dropped frames, each thread's busy share. Machines:
  Intel UHD at 2x (single player and splitscreen), RTX at 3x, two cores at
  1x.
- **Success:** the Intel UHD at 2x with 0 late frames in single player, and
  60 new pictures a second in splitscreen at 2x. Two cores at 1x with fewer
  late frames than today's 57 of 2,800.
