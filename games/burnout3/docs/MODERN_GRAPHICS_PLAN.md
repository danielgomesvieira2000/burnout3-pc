# Modern graphics: an Xbox 360-era look

Written 2026-10-21 (night). The user asked for ambient occlusion, better car
reflections, a modern speed blur in place of the current one, better
lighting and shadows, and if possible modern explosions and sparks. The goal
is a race that looks like a later Xbox 360 game. This plan builds on
GRAPHICS_PLAN.md (the game's render order, its switches and the effect
parameters measured there), which it does not repeat.

## Status (2026-10-22, 01:30)

Done, and MODERN by default (`graphics=original` restores the game's look;
the recorded runs pin it, all as recorded):

| Effect | Setting | How |
| --- | --- | --- |
| Ambient occlusion | AMBIENT OCCLUSION ON | Scalable AO at half resolution before the fog; the player's car at a third |
| Sun shadows | SHADOWS MODERN | Rays through the depth buffer to a high world-fixed sun, in the AO pass; the soft blob kept for contact; none on the player's own car |
| Speed blur | MOTION BLUR MODERN | Reprojection with the camera's previous place (view matrix); 1/100 s shutter, 1/50 boosting; the camera's turning at half; the player's car kept sharp (an ellipsoid at 0x0064E710) |
| Glow | BLOOM MODERN | 6-level soft-knee chain at the internal resolution |
| Grade | LIGHTING MODERN | Shoulder, contrast 1.04, saturation 1.08, vignette, warmth |
| Reflections | REFLECTIONS MODERN | The envmap at the internal resolution (a scale rule); the paint's reflection scaled by a Fresnel term from its sphere-map radius |

**Measured.** At 3x on the RTX 4070 Laptop the race holds 60 pictures a
second with the GPU 37-38% busy, the same as ORIGINAL. The effects cost
about 0.4-1.1 ms a picture.

**Findings that shaped it:**

- The 3D is drawn at half intensity, and the display copy doubles it. The
  picture's white is 0.5 in the target (parameter p9 of the glow and the
  grade).
- The Fresnel term cannot scale the blend's share. That needs the target in
  the shader, and a copy of it for each of the reflection pass's thousands of
  triangles: the race fell to 25 pictures a second. The term scales the
  reflection's colour instead.
- The race's RwCamera Z mapping is Z = 5.03e6 / w + 1175 (near 0.3, metres).
  The camera's place comes from its view matrix: its frame holds the last
  view's.

**Splitscreen.** The graphics manager's block is shared by both views
(index 0), and the camera is the last view's while effects are written. Split
views therefore keep the game's blur, bloom and envmap; AO and the grade run.

**Not done.** Particles: the fire and sparks are a CLUT'd 4-bit atlas
(TBP 0x3B20/0x35E0); a replacement needs its layout. A generated glow
sprite for the 32x32 gradient was tried and dropped, because that gradient
is also the cars' glints, which washed out. Also not done: soft particles,
and the track's own sun direction.

Every effect is a PC SETTINGS value. ORIGINAL draws exactly what the game
draws, so the recorded runs keep their pictures (their `.args` pin ORIGINAL).
MODERN becomes the default for players once each effect holds up.

## What the effects need, and the one engine piece they share

All of them work on the scene's picture **and its depth**, at the internal
resolution, at a point **inside** the game's frame:

- AO before the fog and the glow.
- The blur before the HUD.
- The bloom where the game's own bloom runs.

The present-time post chain (FXAA/SMAA) runs after the HUD and has no depth,
so it cannot host them.

GRAPHICS_PLAN found that an EE-side marker cannot be ordered from outside
the game, because packets go through the scratchpad and are flushed in the
middle of functions. There is a way round that: write the marker **into the
game's own packet stream**, with the game's own packet API, from inside a
wrapped game function.

- The cursor is gp-0x5EB0. `func_001F3080(flags, qwords)` reserves room.
  0x00264920 shows the pattern: a GIFtag with A+D, then register writes.
- The marker is a set of A+D writes to **GS register addresses 0x70-0x7F**,
  which no GS has. The console's GS, PCSX2 and the software GS all ignore
  them, so the stream stays valid.
  - 0x70-0x7D carry parameters (64 bits each).
  - 0x7E runs the effect: an id, plus the scene's FRAME/ZBUF and view
    rectangle, or the parameters say where.
- **Engine.** `gs::Gs` hands writes to 0x70-0x7F to an `on_effect` callback.
  The GPU bridge resolves the target and depth image (as the channel shuffle
  does), marks them drawn, and calls `GsRenderer::RunEffect(GsEffect)`. That
  passes through the threaded renderer to compute passes in
  `gs_renderer.cpp`. Each effect is one compute shader or a few, with
  scratch images of its own at the internal resolution.
- **For free:** the markers go into GS recordings (F12 `replay.bin`,
  `--gs-replay`), so each effect can be tuned on one captured race frame. A
  replay takes seconds, where the race takes two minutes.
- **Draw tags.** Some effects change how particular draws are drawn, not the
  picture after them: the car paint's reflection pass, and particles. A
  marker sets a tag on the draws that follow, up to a marker that clears it.
  The tag reaches `gs.frag` as a state bit.

## The effects

### 1. Ambient occlusion (AO: OFF / ON; default ON)

- **Where.** At the fog (wrap 0x002419C0: the marker, then the fog). The
  world, the cars and the blob shadows are drawn by then. Fog, glow and
  HUD come after, so the AO fades into the haze as it should and never
  darkens the HUD.
- **What.** A GTAO/HBAO-style horizon search on the depth buffer:
  - normals rebuilt from depth;
  - 8 directions x 4-6 steps, at half resolution;
  - a 4x4 blue-noise rotation, a depth-aware (bilateral) blur, upsampled;
  - multiplied into the colour.

  The car's underside and wheel arches, kerbs, walls meeting the road and
  the traffic's contact all get the grounding the PS2 lacks.
- **Depth.** The GS keeps the larger Z, so Z = A/w + B. Linear depth comes
  from A and B, measured once (from the projection the game sets up, or fit
  from a capture). The radius is in world units through that.
- **Cost target:** under 1 ms at 3x (1920x1344) on the RTX.

### 2. Speed blur (MOTION BLUR: ORIGINAL / MODERN / OFF; default MODERN)

- **Where.** The radial blur 0x002FA9A0 and the directional blur 0x00426C10,
  both wrapped. MODERN skips the game's and emits the marker in their place,
  so it runs at the same point (after the glow, before the HUD).
- **What.** A per-pixel blur along each pixel's own motion. For a camera
  moving forward, a still point moves on screen away from the focus of
  expansion, by (p - focus) x speed / depth. Near things (walls, the road's
  edge, passing traffic) streak; the horizon stays sharp. A real camera does
  this, a zoom blur does not.
  - **Speed:** the camera's travel per step, from its values (0x00517030:
    where it is +0x30, the way it looks +0x20, field of view +0x14; player 2
    +0xBB0).
  - **Turning:** the change in look direction adds a sideways shift.
  - **The player's car stays sharp.** It moves with the camera: a
    depth-range mask (the car's distance from the chase camera) plus soft
    edges.
  - **Boost:** the game's own boost value (the radial blur's alpha) adds
    strength. Boosting, the blur is strong at the edges; cruising it is
    subtle.
  - **Quality:** 12-16 taps with interleaved-gradient-noise jitter (no
    banding), weighted by depth, so the background does not bleed over
    nearer things.
- **The directional blur** (the camera's) is replaced the same way.

### 3. Reflections (REFLECTIONS: ORIGINAL / HIGH; default HIGH)

The cars' reflections today are a 256x256 sphere map, warped from three
128x128 faces each frame. It is sampled with no mips, so it is blurry and
shimmers.

- **Sharper.** The envmap pages (faces FBP 0x18B/0x193/0x19B, their Z
  0x16B-0x17B, the sphere map FBP 0x16B TBW 4) are kept at the internal
  scale instead of 1x. This is GRAPHICS_PLAN's step 4: a per-target scale
  rule beside `ScaleOf`, set by the game.
- **Fresnel.** A draw tag on the paint's context-2 pass (TEX0 0x2D60): the
  sphere map's own coordinate gives the angle. At its centre the paint faces
  the camera; at its rim the view is grazing, and N.V = sqrt(1 - r^2). The
  reflection's blend weight is scaled by Schlick's term: little face-on,
  strong at the edges, like a lacquered body. A small floor is kept, so
  paint never loses its colour.
- **Later.** Mips of the sphere map, for the shimmer at grazing angles.

### 4. Lighting and shadows (LIGHTING: ORIGINAL / MODERN; SHADOWS gains MODERN)

The PS2 game lights the world from baked vertex colours, and shadows are
blob quads.

- **Screen-space sun shadows** (SHADOWS MODERN). From each pixel, a march
  through the depth buffer toward the sun's direction on screen. An occluder
  in the way puts the pixel in shadow. This gives car-shaped shadows on the
  road, and traffic, barriers and walls casting onto what is behind them,
  within what is on screen. Applied with AO, before the fog, together with a
  softened blob for contact.
  - The sun's direction: the track's own (to find: the sun glare at stage 4
    of the render has a position, and so does the envmap's sun blob), else a
    high fixed sun per track.
- **Modern bloom** (BLOOM gains MODERN). The game's 160x112 glow becomes a
  6-level downsample/upsample chain at the internal resolution:
  - a soft-knee threshold from the track's values (BLOOM's strength scaling
    them);
  - a little temporal smoothing from the persistence.

  The result is wide, smooth halos around the sun, lights and sparks.
- **Grade** (LIGHTING MODERN). A filmic tonemap shoulder on the bright end,
  a gentle contrast S-curve, a little saturation and a soft vignette. This is
  the "360 look", applied at the end of the 3D, before the HUD (with the
  blur marker, or the bloom's when there is no blur).

### 5. Particles: explosions and sparks (PARTICLES gains MODERN, if time allows)

- **Soft particles.** A draw tag around the particle rendering (the
  function to find, from the particles switch +0x6C). Tagged draws fade
  where they meet the scene's depth, so smoke and fire never show a hard
  line where they cut the road or a car. `gs.frag` reads the depth at the
  pixel; the depth attachment is read-only for those draws, which never
  write Z.
- **Sparks as light.** Sparks are drawn additively, to an HDR-like intensity
  the bloom picks up. With MODERN bloom they glow without a change to them.
  An additive boost on tagged spark draws comes next.
- **Generated textures.** The spark, flare and fire textures are replaced
  with finer ones made in code (no game art redistributed), as the SOFT
  blob is. The texture names come from the texture pack's dump.

## Order

1. **Engine: effect markers.** The register range, the callback, the bridge,
   `RunEffect` through the threaded renderer, and an empty "tint" effect to
   prove the order. **Game:** `EmitEffect`. A replay of a captured race
   frame shows the effect on the scene, not on the HUD.
2. **The depth mapping** (A, B) and the cameras' values for the blur.
3. **AO.**
4. **MODERN motion blur.**
5. **MODERN bloom + grade.**
6. **Screen-space sun shadows.**
7. **Reflections:** per-target scale, then fresnel (the draw tag).
8. **Particles**, as far as time allows.
9. **Measure**, then optimise: the remaining time goes to performance and
   input delay (PERFORMANCE_PLAN.md).

## Measuring

- **Per effect: the GPU time.** Each pass gets a profiler mark
  (`PS2HOST_GPU_PROFILE`). It is taken from a replayed capture repeated
  (`PS2HOST_REPLAY_REPEAT`), at 3x on the RTX 4070 Laptop and 2x on the
  Intel UHD.
- **In the race:** the real-time 3x race (frame times, late pictures), all
  effects on vs ORIGINAL, single player and splitscreen.
- **Budget.** Under 3 ms for everything at 3x on the RTX. On the UHD the
  effects must be cheap enough to be worth it at 2x, or they stay OFF there:
  the defaults look at the GPU.
- **Accuracy.** ORIGINAL keeps every recorded run: GPU regressions after
  each engine step (race, race_training, car_select), and the soft runs
  before a tag.
