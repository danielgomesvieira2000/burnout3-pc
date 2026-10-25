/*
 * Burnout 3 at 60 pictures a second where it drew 30: its menus (and the
 * movies they play) and its crashes - Nehalem's "60 FPS Menus" and "60 FPS
 * Crashes" PCSX2 patches (github.com/Nahelam/PS2-Game-Mods, Burnout 3
 * Takedown/Graphics, SLUS-21050), always on.
 *
 * The game draws a picture every blank or every second one: 0x001F4290 sets
 * which (1 or 2), called by the frontend as a screen starts (0x001D3EE0) and
 * by the main loop as a crash's slow motion starts and ends (0x00132090).
 * The patches make both always choose 1; a movie, timed by the picture,
 * then advances half as far each one (0x00130D50).
 *
 * At half rate the race also goes on two ticks a picture, not one (the main
 * loop, 0x00132F48: the flag +0x2DA66). Splitscreen asks for half rate for
 * the whole race, a crash for its slow motion. At every blank and one tick
 * a picture, a PC that makes fewer than 60 pictures a second ran the race
 * slower than the console (splitscreen at ~48: at 80% of its speed). So
 * where the game asks for half rate, a picture that took two blanks is
 * followed by two ticks: 60 pictures a second where the PC keeps up, the
 * console's time where it does not - never slower than its own 30.
 */

#include <algorithm>
#include <cstring>

#include "ps2host/host.h"
#include "ps2host/run.h"

void HighFpsBlank();            // high_fps.cpp
void MusicWatchdog();           // overrides.cpp

// The frontend's frame rate as a screen starts; the main loop's as a crash
// slows; a movie screen as its movie starts (see each below).
void F_00132090(lifted::Context& c);
void F_00130D50(lifted::Context& c);
// The vertical blank's handler (see BlankHandler).
void F_001F1CE8(lifted::Context& c);

namespace {

constexpr u32 kSetInterval = 0x001F4290u;  // a0: a picture every 1 or 2 blanks

/* 0x001D3EE0(screen, half): a frontend screen's frame rate - at half
   (a1 != 0) a picture every second blank, its rate (60, or 50 on a PAL
   television) halved. Here every blank, at the full rate; the mode it
   records (gp-0x5974) is the game's own. The patch at 0x001D3F2C. */
void MenuRate(lifted::Context& c) {
    const u32 entry_ra = u32(c.r[31]);
    const u32 gp = u32(c.r[28]), screen = u32(c.r[4]);
    const bool half = c.r[5] != 0;
    const bool pal = generated::ReadS8(gp - 0x5D60u) != 0;
    c.r[4] = 1;
    lifted::Call(c, kSetInterval, 0x001D3F60u);
    generated::Write32(gp - 0x5974u, pal ? (half ? 3u : 1u) : (half ? 2u : 0u));
    c.r[4] = pal ? 50u : 60u;
    lifted::Call(c, 0x001D3D30u, 0x001D3F6Cu);
    c.r[4] = lifted::Sx32(screen + 0x7040u);
    lifted::Call(c, 0x001D3C90u, 0x001D3F74u);
    c.r[4] = lifted::Sx32(screen + 0x7040u);
    lifted::Call(c, 0x001D3C50u, 0x001D3F7Cu);
    c.r[31] = entry_ra;
}

/* 0x001F1CE8 (the vertical blank's handler, a0 the cause): with its own
   data pointer (*(gp-0x5E24)), a picture ready to show (-0x5EA1) flipped at
   the start of a blank (cause 2) once the interval's blanks have gone by
   (its count, -0x5E2F, stops at the interval: at 1 it cannot tell). Here,
   as each flips, the blanks it took since the one before - counted here. */
u32 g_picture_blanks = 1;

void BlankHandler(lifted::Context& c) {
    static u32 blanks = 0;
    if (u32(c.r[4]) == 2) {
        HighFpsBlank();
        MusicWatchdog();        // (every blank, every mode: a load waits on the music too)
    }
    const u32 data = u32(generated::ReadS32(u32(c.r[28]) - 0x5E24u));
    const bool start = u32(c.r[4]) == 2;
    const bool ready = generated::ReadU8(data - 0x5EA1u) != 0;
    if (start) ++blanks;
    F_001F1CE8(c);
    if (start && ready && generated::ReadU8(data - 0x5EA1u) == 0) {
        g_picture_blanks = std::max(1u, blanks);
        blanks = 0;
    }
}

/* 0x00132090(game): while racing (0x004EB1E0), the half rate asked for
   (+0x2DA65: a crash's slow motion, splitscreen's whole race) is followed
   (+0x2DA66: a picture every second blank, two ticks each). Here a picture
   every blank, each frame - the patch at 0x001320D8 (the change seen, a
   branch to the every-blank call) - and followed only as ticks: two after a
   picture that took two blanks (see above), one otherwise. */
void CrashRate(lifted::Context& c) {
    const u32 entry_ra = u32(c.r[31]);
    const u32 asked = u32(c.r[4]) + 0x30000u - 0x259Bu, followed = asked + 1;
    generated::Write8(followed, generated::ReadU8(asked));   // no change seen
    F_00132090(c);
    const bool racing = generated::ReadU8(0x004EB1E0u) != 0;
    const bool behind = racing && generated::ReadU8(asked) != 0 && g_picture_blanks >= 2;
    generated::Write8(followed, behind ? 1u : 0u);
    if (racing) {
        u64 saved[6];
        std::memcpy(saved, &c.r[2], sizeof saved);           // v0..a1 as it left them
        c.r[4] = 1;
        lifted::Call(c, kSetInterval, entry_ra);
        std::memcpy(&c.r[2], saved, sizeof saved);
    }
    c.r[31] = entry_ra;
}

/* 0x00130D50(screen): a movie screen starting its movie (+0x2F8 2, or 0x18
   first) - the movie a picture's time (+0x20C) at the game's rate (gp-0x7F8C;
   gp-0x7F90 on a PAL television) once it plays (+0x2F8 0x17). Here half of
   it, at every blank. The patch at 0x00130DD8. */
void MovieRate(lifted::Context& c) {
    const u32 screen = u32(c.r[4]), gp = u32(c.r[28]);
    const s32 before = generated::ReadS32(screen + 0x2F8u);
    F_00130D50(c);
    if ((before == 2 || before == 0x18) && generated::ReadS32(screen + 0x2F8u) == 0x17) {
        const u32 bits = generated::ReadU32(gp - 0x7F8Cu);
        float step;
        std::memcpy(&step, &bits, 4);
        step /= 2.0f;
        u32 half;
        std::memcpy(&half, &step, 4);
        generated::Write32(screen + 0x20Cu, half);
    }
}

}  // namespace

// The blanks the last picture took (high_fps.cpp's ticks).
unsigned PictureBlanks() { return g_picture_blanks; }

// The movie screen's start as registered here, for lan.cpp's debug probe of
// the same function to call in turn.
void SixtyFpsMovieRate(lifted::Context& c) { MovieRate(c); }

void RegisterSixtyFps() {
    ps2host::Override(0x001D3EE0u, MenuRate, "a frontend screen's frame rate: 60 FPS menus");
    ps2host::Override(0x00132090u, CrashRate, "a crash's slow motion, splitscreen: at 60 FPS");
    ps2host::Override(0x001F1CE8u, BlankHandler, "the vertical blank's handler: the blanks a picture took");
    ps2host::Override(0x00130D50u, MovieRate, "a movie screen's start: its movie at 60 FPS");
}
