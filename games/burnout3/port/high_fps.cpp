/*
 * 120 pictures a second, or the screen's own rate: the engine's high frame
 * rate (ps2host/high_fps.h) in Burnout 3 - where its things are and the
 * places of its loop it is called from (docs/HIGH_FPS_PLAN.md: how each was
 * found).
 *
 * The race ticks 60 times a second - its physics, clock, crashes and AI
 * take a sixtieth each - and stays so; the menus are the same loop (the main
 * loop's state 5: the frontend over the race's, its backdrops the race's
 * world) and tick with it. Its main loop (0x00132600) runs the race's ticks
 * in a loop, as many as the last picture took (0x00376570: one a picture,
 * at least): above 60, as many as 60 a second asks for instead
 * (HighFpsSteps). What it runs once a frame after them (0x00131F90: a
 * crash's camera, its clock) runs only after a tick. The ticks are counted
 * by the race's own count (gp-0x5C5C, counted up in the loop of them:
 * 0x00132DC8).
 */

#include "ps2host/high_fps.h"
#include "ps2host/host.h"

unsigned PictureBlanks();               // sixty_fps.cpp
void F_00376570(lifted::Context& c);
void F_00131F90(lifted::Context& c);
void F_002225C0(lifted::Context& c);

namespace {

/* Where the transforms live (engine/tools/find_transforms.py, in a race, a
   crash and the garage): the cars, their wheels, the traffic between
   0x00517000 and 0x00660000; the camera's RwFrame about 0x01ED0E80 - in a
   race made anew in the draw (the camera's values are placed instead), in
   the garage (the car select) set in the ticks: its modelling matrix moved,
   the view moves. Not a crash's flying parts (about 0x00825000: the movies'
   buffers share that memory - placed there, a movie broke).

   The cameras: the draw makes each view (0x00227360, three times a picture)
   from its camera's own values, set in the ticks (a crash's camera moves on
   its own) - player 1's at 0x00517030, player 2's (splitscreen) 0xBB0 on:
   the field of view (+0x14), the way it looks (+0x20), where it is (+0x30).
   A cut further than a car moves in a tick: 50 units. */
ps2host::HighFpsGame Burnout() {
    ps2host::HighFpsGame game;
    game.transforms = {{0x00500000u, 0x00700000u}, {0x01ED0000u, 0x01EE0000u}};
    game.cameras = {{0x00517030u, 16, 12, 8}, {0x00517030u + 0xBB0u, 16, 12, 8}};
    game.cut = 50.0f;
    game.subjects = {0x0064E710u, 0x0064E710u + 0x27D0u};     // the players' cars (the trace's)
    return game;
}

/* 0x00376570(timer, least, most): the ticks the race's next frame runs -
   the timer's since the last call, at least `least`. Called so by the
   main loop (0x00132600, returning to 0x00132F64 or 0x00132F7C); above
   60 pictures a second, as many as 60 a second asks for the blanks the
   picture took (so a picture late is caught up, as at 60), up to `most`. */
void Ticks(lifted::Context& c) {
    const u32 ra = u32(c.r[31]);
    const s32 most = s32(c.r[6]);
    F_00376570(c);
    if (ra != 0x00132F64u && ra != 0x00132F7Cu) return;
    const int ticks = ps2host::HighFpsSteps(PictureBlanks(), most);
    if (ticks >= 0) c.r[2] = u64(s64(ticks));
}

/* 0x002225C0, the main loop's first call each frame (0x00132750), before
   the race's ticks: what the last picture placed, back as the ticks left
   it. */
void FrameStart(lifted::Context& c) {
    ps2host::HighFpsFrameStart();
    F_002225C0(c);
}

/* 0x00131F90(game): the race frame's logic after its ticks, once a frame -
   a crash's: its camera's moves, Impact Time's clock. At 60 a frame is a
   tick; above, a frame that ran no tick does not run it either (run at
   every picture, at 120 the crash went at twice its speed) - and one that
   did has its transforms taken down, for the pictures until the next. */
void AfterTicks(lifted::Context& c) {
    if (!ps2host::HighFpsFrameSteps(generated::ReadU32(u32(c.r[28]) - 0x5C5Cu), PictureBlanks())) return;
    F_00131F90(c);
    ps2host::HighFpsStepped();
}

}  // namespace

/* Each picture (mods.cpp's Frame: the race's ticks run, its draw next). */
void HighFpsFrame() { ps2host::HighFpsPicture(); }

/* Each vertical blank's start (sixty_fps.cpp's BlankHandler, in every mode
   of the game): back to 60 once the race's frames have stopped (the menus
   and movies after a race ran at 120 when left there). */
void HighFpsBlank() { ps2host::HighFpsBlank(); }

void RegisterHighFps() {
    ps2host::HighFpsConfigure(Burnout());
    ps2host::Override(0x00376570u, Ticks, "the race's ticks a frame: 60 a second at 120 pictures and more");
    ps2host::Override(0x002225C0u, FrameStart, "the main loop's frame starts: the pictures' placing put back");
    ps2host::Override(0x00131F90u, AfterTicks, "the race frame's logic after its ticks: once a tick at 120 and more");
}
