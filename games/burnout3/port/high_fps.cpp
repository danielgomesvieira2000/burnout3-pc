/*
 * 120 pictures a second in a race (docs/HIGH_FPS_PLAN.md), FRAME RATE in
 * pc_settings.ini (frame_rate=120; 60 the game's own).
 *
 * The race steps 60 times a second - its physics, timers and AI take a
 * sixtieth each - and stays so. While a one-player race runs (its world
 * stepping: widescreen.cpp's RaceWorld; not in splitscreen, whose two views
 * already take what a machine has), the vertical blanks come twice as often
 * (SetFastForward(2)): the game draws a picture at each, and its world
 * (0x0019A550) is stepped at every second one only. The picture between two
 * steps is drawn from the world as the last step left it (step 1 of the
 * plan: what two pictures a step cost, and what of the game moves with the
 * pictures rather than the steps).
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

unsigned long long RaceWorldCalls();    // widescreen.cpp

namespace {

constexpr u32 kPlayers = 0x0064D3A0u;   // the race's players (2 or more: splitscreen, party modes)

bool g_active = false;
unsigned long long g_pictures = 0;      // since 120 began: the world steps on the even ones

/* FRAME RATE: PS2HOST_FRAME_RATE, else the setting. */
bool Wanted() {
    static const char* env = std::getenv("PS2HOST_FRAME_RATE");
    if (env) return std::atoi(env) == 120;
    return ps2host::Setting("frame_rate", "60") == "120";
}

/* A race running: its world asked to step within the last few pictures (as
   controls.cpp's Racing; still in the pause menu, the frontend, a loading
   screen). */
bool Racing() {
    static unsigned long long seen = 0;
    static int still = 99;
    const unsigned long long calls = RaceWorldCalls();
    still = calls != seen ? 0 : still + 1;
    seen = calls;
    return still <= 8;
}

}  // namespace

/* Each picture (mods.cpp's Frame): 120 on or off as the race and the
   setting say. */
void HighFpsFrame() {
    const bool want = Racing() && Wanted() && generated::ReadS32(kPlayers) < 2;
    if (want == g_active) return;
    std::fprintf(stderr, "[high fps] %s\n", want ? "120 pictures a second" : "60 pictures a second");
    g_active = want;
    g_pictures = 0;
    ps2host::SetFastForward(want ? 2 : 1);
}

/* The race's world about to step (RaceWorld): not at every second picture
   while at 120. */
bool HighFpsSkipWorld() {
    if (!g_active) return false;
    return (g_pictures++ & 1) != 0;
}
