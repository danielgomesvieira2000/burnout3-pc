/*
 * The game's hidden development menu (Unlock All, Lock All, the race and
 * crash awards shown, ...), opened from PC SETTINGS' DEVELOPER MENU - found
 * by Nehalem, whose PCSX2 patch (github.com/Nahelam/PS2-Game-Mods, Burnout 3
 * Takedown/Development Menu) shows it in place of EXTRAS.
 *
 * The frontend is a state machine (0x01EA2970: +0x4 the screen showing, +0x8
 * its context; a screen an object whose +0xC descriptor's +0x8 handler takes
 * its events: 0 made, 2 left (a3 the next), 3 entered (a3 the last), 4 a
 * frame, 5 a message (a3 its kind: 2 chosen, 3 back)). The development menu's
 * handler (0x00425330) is in the game but no screen's; the patch gives it
 * EXTRAS' screen (0x01EA2B68, its descriptor's handler at 0x004E0568). Here
 * that screen has it only when the port opens it: from PC SETTINGS, and back
 * to it - DRIVER DETAILS' EXTRAS stays the game's.
 */

#include <cstring>
#include <initializer_list>

#include "ps2host/host.h"
#include "ps2host/run.h"

// The frontend's message to its screen; EXTRAS' handler, the development menu's.
void F_002176D0(lifted::Context& c);
void F_00456900(lifted::Context& c);
void F_00425330(lifted::Context& c);
void ExtrasPage(lifted::Context& c);     // extras.cpp: the game's EXTRAS without the NFS demo's row

namespace {

constexpr u32 kFrontend = 0x01EA2970u;          // the state machine
constexpr u32 kExtras = 0x01EA2B68u;            // EXTRAS' screen
constexpr u32 kDriverDetails = 0x01EA2A68u;     // DRIVER DETAILS' screen
constexpr u32 kChosen = 0x01E95B58u;            // the development menu's row chosen
constexpr u32 kFlow = 0x01E90240u;              // the frontend's fades between screens

struct {
    bool requested = false;     // DEVELOPER MENU chosen: opened as the main menu fades out
    bool opening = false;       // EXTRAS' screen entered by the port
    bool showing = false;       // EXTRAS' screen is the development menu
    bool returned = false;      // back at PC SETTINGS
    u32 from = 0;               // the screen it was opened from (the main menu's)
} g_dev;

u32 Word(u32 at) { return u32(generated::ReadU32(at)); }

/* Calls a guest function, its arguments in a0-t0; the caller's registers as
   they were. */
void GuestCall(lifted::Context& c, u32 target, std::initializer_list<u32> arguments) {
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    int r = 4;
    for (u32 a : arguments) c.r[r++] = s64(s32(a));
    lifted::Call(c, target, u32(saved[31]));
    std::memcpy(c.r, saved, sizeof saved);
}

/* The frontend to `screen`, as the game's screens go from one to the next
   (the development menu's own way back, 0x00425634 on): the one showing left (the
   machine's method +0x18), the new one entered (+0x1C). */
void GoTo(lifted::Context& c, u32 screen) {
    if (Word(kFrontend + 0x4u) == screen) return;
    const u32 methods = Word(kFrontend);
    GuestCall(c, Word(methods + 0x18u), {kFrontend, screen, 0});
    const u32 last = Word(kFrontend + 0x4u);
    generated::Write32(kFrontend + 0x4u, screen);
    if (const u32 clock = Word(kFrontend + 0x10u)) {
        const u32 now = Word(clock + 0x20u);
        generated::Write32(kFrontend + 0x14u, now);
        generated::Write32(screen + 0x8u, now);
    }
    GuestCall(c, Word(methods + 0x1Cu), {kFrontend, last, 0});
}

/* 0x002176D0(machine, kind, a2): a message to the screen showing (its
   event 5) - the frontend's fade out over (0x0030D960), kind 0 a row chosen.
   A DEVELOPER MENU chosen: the main menu's message is the development
   menu's opening, in its place. */
void Message(lifted::Context& c) {
    if (u32(c.r[4]) == kFrontend && g_dev.requested && u32(c.r[5]) == 0) {
        g_dev.requested = false;
        g_dev.from = Word(kFrontend + 0x4u);
        g_dev.opening = true;
        GoTo(c, kExtras);
        g_dev.opening = false;
        return;
    }
    F_002176D0(c);
}

/* 0x00456900(screen, event, context, a3): EXTRAS' handler - the development
   menu's while the port has it open. Its way back (Triangle; a row with no
   screen of its own, Unlock All and the like, chosen) goes to DRIVER DETAILS:
   opened from PC SETTINGS, back there instead. Its awards (Show All Race
   Awards, Show All Crash Awards) go on as the game has them: the tour's
   end, its credits and rewards. */
void Extras(lifted::Context& c) {
    const u32 screen = u32(c.r[4]), event = u32(c.r[5]), a3 = u32(c.r[7]);
    if (screen != kExtras) {
        ExtrasPage(c);
        return;
    }
    if (event == 3) {
        g_dev.showing = g_dev.opening || (g_dev.showing && a3 != kDriverDetails);
        if (!g_dev.showing) g_dev.from = 0;
    }
    if (!g_dev.showing) {
        ExtrasPage(c);
        return;
    }
    if (event == 5 && g_dev.from) {
        const s32 chosen = s32(generated::ReadS32(kChosen));
        if (a3 == 3 || (a3 == 2 && chosen != 2 && chosen != 3)) {
            g_dev.returned = true;
            GoTo(c, g_dev.from);
            return;
        }
    }
    F_00425330(c);
}

}  // namespace

/* PC SETTINGS' DEVELOPER MENU chosen (overrides.cpp, in the main menu's
   input): the main menu fades out as for a row of its own (0x0030D560, as
   0x003182BC does), the development menu in. */
void OpenDevMenu(lifted::Context& c) {
    g_dev.requested = true;
    GuestCall(c, 0x0030D560u, {kFlow, 0, 0});
}

/* Back from it, once: PC SETTINGS shown again. */
bool DevMenuReturned() {
    const bool returned = g_dev.returned;
    g_dev.returned = false;
    return returned;
}

void RegisterDevMenu() {
    ps2host::Override(0x002176D0u, Message, "the frontend's message: PC SETTINGS' DEVELOPER MENU opened");
    ps2host::Override(0x00456900u, Extras, "EXTRAS' screen: the development menu, from PC SETTINGS");
}

