# Graphics options: bloom, motion blur, reflections, shadows

Status: **plan, not started**. Written 2026-10-19 from a read of the lifted
code, two traced race frames (`race.args`, frames 18001 and 18400) and
Nahelam's B3Mod and research notes (github.com/Nahelam/B3Mod,
PS2-Game-Mods/Burnout 3 Takedown/Research: PAL symbols, mapped here to
SLUS-21050). The work isn't done yet. This sets out what the game does, what
each option would change, the engine API it needs, and the order.

Every option's ORIGINAL value draws exactly what the game draws today, so the
regressions keep their recordings.

## What the game does (measured, not assumed)

The race's render per view, `CB3World::Render` (0x00276300), in order:

1. Track before vehicles (0x0021C780). This also draws the envmap's three faces.
2. Envmap faces distorted into the sphere map (0x00222CA0).
3. Vehicles.
4. Track after vehicles, full-screen specular (sun glare), shadows (0x003033F0).
5. Fog (0x002419C0).
6. Bloom, `CB3Bloom::Render` (0x002659C0).
7. Either directional blur (0x00426C10), when the camera asks for it, or radial
   blur (0x002FA9A0).
8. Then the HUD.

**The game's own switches.** Byte switches sit at 0x00665EC0. B3Mod's menu
names them, and all default to 1 (set by 0x0019EBF0). Nothing else writes
them.

| Switch | Byte |
| --- | --- |
| Shadows | 0x00665F27 |
| Bloom | 0x00665F28 |
| Fog | 0x00665F29 |
| Blur | 0x00665F2A (gates radial *and* directional) |
| Track into envmap | 0x00665F20 |
| Instances into envmap | 0x00665F22 |

**Bloom.** A bright pass at 320x224: (C - T) rescaled, T = 255 x threshold.
It is downsampled to 160x112, blurred with four shifted copies, and kept in an
accumulator with feedback (trails). It is then added back over the frame,
upscaled 4x bilinear.

Its parameters live in the view struct at +0x50 (P1 0x00666090, P2
0x00666130):

| Offset | Meaning | Default | Source |
| --- | --- | --- | --- |
| +0x50 | Threshold | 0.75 | Copied each frame from the track (0x0064C9A0) by 0x0030A9D0 |
| +0x54 | Intensity | 1.0 | Copied each frame from the track (0x0064C9A4) by 0x0030A9D0 |
| +0x58 | Persistence | 0.5, up to 0.9 while boosting | Set by 0x0030A9D0 |

The 160x112 layer is why the glow looks blocky and soft at any internal
resolution.

**Radial (boost) blur.** A single-frame 8-tap zoom blur in a half-size buffer
(zooms s^0..s^7, s = 0.995 - 0.01 x boost). It is faded in by a mask that is
clear at the centre and blurred towards the edges, then stretched over the
frame. Its parameters are in the view struct at +0x00..+0x1C, filled each
frame by 0x0030ABA0:

| Offset | Meaning |
| --- | --- |
| +0x00/+0x04 | Centre |
| +0x08/+0x0C | Mask scale |
| +0x10/+0x14 | Zoom |
| +0x1C | Alpha = boost |

Off boost the alpha is 0, so it draws nothing. The directional blur (struct
+0x60) is built the same way and is not analysed yet.

**Reflections.** Every frame the track draws three 128x128 faces into the
envmap:

- Faces at FBP 0x18B, 0x193 and 0x19B; their Z at 0x16B, 0x173 and 0x17B.
- They hold the sky dome, the sun blob, track sectors, and the instances
  flagged 0x2. No cars or traffic are drawn into them.
- A warp mesh (1,332 triangles) distorts them into a 256x256 sphere map
  (FBP 0x16B, TBW 4).
- The car paint samples it in a second, context-2 pass: TEX0_2 0x2D60,
  bilinear, no mips, blended by the frame's alpha (the car's reflectivity).
- The envmap costs about 11% of the frame's GS primitives.
- The same pages are reused later as a 320-wide frame copy.
- There are no world (road) reflections.

**Shadows.** Blob shadows only: one oriented quad per object, drawn last.

- The quad is an 8-vertex 2x4 strip, so the middle stretches with car length.
- Its texture is "blobbyshadow", 64x64 PSMT8 at 0x3C40, blended at alpha about
  0x5A, Z test without writes.
- Race cars, visible traffic and one more list get one. That is about 7
  shadows and 45 triangles a frame.
- The quad is flat, so on slopes and kerbs it floats or cuts in.
- The world's shadows are baked into the track's vertex colours.

**Detail (LOD).** The car LOD distances {15, 30, 60} and the traffic ones
{20, 80} are copied to 0x00665F30..0x00665F40 by 0x0019E9A0. Nahelam's force-LOD
patch (0x00151ABF = 0x10, 0x00151B78 = `li a3,4`) picks a level 4 that the
distances never reach. It is presumably an extra, most detailed model; not
checked.

**The engine today** (from the engine survey):

- **Post passes.** FXAA, SMAA and SSAA run at present time, on the shown
  colour image. That is after the game's effects and after the HUD. There is
  no depth there and only one in/out image pair, so only one pass runs.
- **Scaling.** `ScaleOf` keeps targets <= 320 or >= 1024 wide at 1x, so every
  effect buffer, envmap face and the sphere map are at the PS2's resolution
  whatever the setting. Images already carry their own scale, and sampling
  honours it, so mixed scales work.
- **What a game cannot do.** It cannot tag a GS draw, run a pass at a point in
  the GS stream, or ask for a scale per target.
- **Replacements.** Texture overrides (`texture_pack::SetOverride`) work at
  any resolution, but only for textures decoded from memory, not for render
  targets.
- **Filtering.** No anisotropic filtering. GS textures are sampled by hand in
  `gs.frag`.

## A bug to fix first

The current BLUR = OFF setting skips 0x002FA9A0, and its help text says it
removes "the speed blur and glow". Neither part of that is right:

- **The glow is not touched.** The bloom is a separate function and stays on.
- **Only one blur goes.** The directional blur still runs. Only the radial blur
  is removed.

The comment at overrides.cpp:144-148 says the same and needs fixing too.

## The options

GRAPHICS page, values as shown in the menu. Each value list has at most four
entries.

| Option | Values | Default |
| --- | --- | --- |
| BLOOM | OFF / ORIGINAL / MODERN | ORIGINAL |
| BLOOM STRENGTH | LOW / ORIGINAL / HIGH | ORIGINAL |
| MOTION BLUR | OFF / ORIGINAL / MODERN | ORIGINAL |
| REFLECTIONS | ORIGINAL / 2X / 4X | ORIGINAL |
| SHADOWS | OFF / ORIGINAL / SOFT | ORIGINAL |
| DETAIL | ORIGINAL / HIGH | ORIGINAL |

The key `blur` keeps its saved meaning: OFF stays OFF.

### 1. Game-only switches and strengths

This needs no engine change.

- **BLOOM OFF / MOTION BLUR OFF.** Write 0 to 0x00665F28 / 0x00665F2A when the
  setting changes, and after 0x0019EBF0 runs. MOTION BLUR OFF then removes
  both blurs, which fixes the bug.
- **BLOOM STRENGTH.** Wrap 0x0030A9D0 and scale the view struct's threshold,
  intensity and persistence after it. For example:
  - LOW: intensity x0.6, persistence x0.7;
  - HIGH: threshold -0.1, intensity x1.3.

  The values are to be tuned by eye against PCSX2 captures. The same
  multiplier drives the MODERN bloom below.
- **DETAIL HIGH.** Scale the LOD distances by 2-3x after 0x0019E9A0, cars and
  traffic.
  - Check level 4 (force-LOD) on its own first: if it is a real model, HIGH
    can use it near the camera.
  - The cost is VU1 time, the port's bound. Benchmark it in splitscreen on the
    iGPU.

### 2. REFLECTIONS 2X / 4X: the envmap at internal resolution

- **Engine.** Add a per-target scale rule beside `ScaleOf`: a game's hint
  "FBP 0x16B..0x1A7 at a fixed scale". It resolves in `TargetFor`/`DepthFor`,
  so a depth matches its target. It goes through the threaded renderer, and on
  change it writes back and drops the affected images, as `Rescale` does.
- **Game.** Ask for 2x or 4x of the faces, their Z and the sphere map.
- **Watch: aliasing.** The 32-wide clears over the same pages, and the
  320-wide reuse as a frame copy, alias the scaled images. Either clear the
  scaled images, or take one reload a frame. The reload is cheap, because the
  warp overwrites the whole sphere map every frame. The engine's alias
  reconciliation must not drop the scaled faces between the face draws and
  the warp.
- **Expected result.** Sharp reflections on the paint at small cost: 4x is
  16x the pixels of 3 x 128² + 256², still under one 640x448 frame.
- **Later: shimmer.** At grazing angles the sphere map shimmers. It could be
  mip-mapped when sampled as TEX0_2 0x2D60 (generate the mips after the warp;
  a `gs.frag` path for that one texture), or the face count could be raised.
  Both are optional.
- **Later: ULTRA.** Draw every instance into the faces, not only the 0x2
  flagged ones: the test at about 0x0021CBF0. Cars stay out. This costs VU1;
  decide from a benchmark.

### 3. SHADOWS SOFT: a better blob

- **First step: a sharper, softer texture.** Replace "blobbyshadow" through
  `texture_pack::SetOverride` with a 256x256 blob the port *generates* in
  code: a smooth falloff of the same shape, so no game asset is redistributed.
  Optionally lift its alpha a little. This is cheap; check it at the cars'
  ends, where the strip's middle stretches.
- **Second step: on the ground.** Tessellate the 8-vertex plane (for example
  8x8) and drop each vertex onto the road, using the game's own ground probe
  (to find) or the car's wheel contacts. This stops the blob floating and
  cutting on slopes and kerbs. It is moderate work in 0x00303170/0x00303A40.
- **Not planned: real projected shadows.** That means each car's silhouette
  from the sun into a shadow map, and the road sampling it. The game has no
  such pass, the cars reach the GS only as VU1 output, and the road would need
  a different shader. It is a big engine project, to come back to after the
  rest.
  - A middle way: render each car's silhouette top-down into a small texture
    and use it as that car's blob. This gives a car-shaped shadow on the
    existing quad. It still needs the car's draws tagged in the GS stream (see
    the effects API below). Research item.

### 4. MODERN bloom and blur: effects of the PC's own, before the HUD

**Yes, a modern blur that looks better than ORIGINAL is possible.** So is a
modern bloom. Both work best as passes the engine runs at the point in the
frame where the game's effect would run, on the scene's target at internal
resolution, with its depth, before the HUD.

**Engine: a game-ordered GPU effect.**

- An API such as `ps2host::GpuEffect(id, params)` puts a marker into the GS
  stream, after the draws already queued.
- The GS thread, on reaching the marker, runs a built-in compute effect on the
  current frame target (and its depth buffer, which already has a sampled
  view).
- The engine supplies the effects; the game passes plain numbers. This keeps
  the engine generic: any game with a glow or a speed blur can use it.
- The display's post chain also needs ping-pong images, so that AA
  (FXAA/SMAA) still runs after these effects.

**MODERN bloom.**

- The game override skips 0x002659C0 and emits the bloom effect with the
  view's threshold, intensity and persistence (and BLOOM STRENGTH).
- **Effect:** a soft-knee bright pass, a 5-6 level downsample/upsample chain
  (dual filter or 13-tap) at internal resolution, a little temporal smoothing
  from the persistence, and an additive composite. Halos are smooth and the
  same at any resolution.
- **Splitscreen:** each view's effect runs in that view's rectangle.

**MODERN motion blur.**

- The override skips 0x002FA9A0 and emits the radial effect with the view
  struct's centre, zoom, mask scale and alpha. Those are the game's own
  boost-driven values, so it blurs exactly when and where the game does.
- **Effect:** a 16-32 tap radial blur at internal resolution with blue-noise
  jitter (no banding), the same centre mask, and, using the depth, the
  player's car kept sharp (the original blurs it too).
- The directional blur gets the same treatment once analysed.

**Glow-buffer upscaling is not planned.** ORIGINAL's glow could instead be
sharpened by asking the 320- and 160-wide effect buffers to scale. The ≤320
rule was made because at 4x they cost a third of the GPU and the glow
ghosted. MODERN replaces it instead.

## Order

1. **Fix the BLUR bug.** Split BLOOM and MOTION BLUR (switch bytes) and add
   BLOOM STRENGTH, with correct help text. Game only; regressions unchanged at
   ORIGINAL.
2. **DETAIL HIGH.** Game only. Benchmark: iGPU splitscreen 2x, RTX 3x; read
   the VIF1 transfers a second, not just "dropped".
3. **SHADOWS SOFT step one.** The generated blob texture.
4. **Engine: per-target scale rule, then REFLECTIONS 2X/4X.** Run all games'
   regressions with the rule unused (it must change nothing), then Burnout's
   at 2X/4X by eye against the 1x pictures.
5. **Engine: game-ordered GPU effect and the post ping-pong, then MODERN bloom
   and MODERN motion blur.** Benchmark on the UHD: a bloom chain at 2x of
   1280x896 is the costliest item here.
6. **Research.** The directional blur's parameters; SHADOWS step two (ground
   probe); car-silhouette shadows.

## How it is checked

- **Accuracy.** Every default (ORIGINAL) keeps today's recordings: the GPU
  runs after each step, and the software runs before the tag.
- **New values.** Each new value gets a scripted run at a boost moment (frame
  18400 in `race.args` is a crash-cam frame; find a boost frame) with
  `PS2HOST_GPU_DUMP_FRAME`. These are compared by eye with ORIGINAL and with
  PCSX2. They are not CRC-recorded until the values settle.
- **Cost.** Each value's cost is benchmarked on both GPUs, single player and
  splitscreen.

## Open questions

- Is LOD level 4 a real model on every car?
- The directional blur's struct, and when the camera picks it over the radial.
- Is the bloom's feedback what makes boosting look smeared, and should MODERN
  keep it?
- Do the 2X/4X reflection faces survive the frame-copy reuse of their pages
  without a reload each time?
