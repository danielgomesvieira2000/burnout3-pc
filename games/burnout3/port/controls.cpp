/*
 * MODERN CONTROLS (PC SETTINGS, CONTROLS): a controller's triggers to
 * accelerate and brake, as racing games have them now, while racing - and
 * the game's own controls everywhere else (its menus, the pause screen, a
 * message box), where Cross and Triangle choose and go back as they always
 * did. The keyboard keeps its keys.
 *
 * The race's controls (found by holding each button through the first
 * race): Cross accelerates, Square brakes and reverses - both read for how
 * far they are pressed - R1 boosts (and, held in a crash, is Impact Time),
 * Triangle changes the camera, L1 looks back, L2 skips to the next EA Trax
 * song. Modern, the controller's are:
 *
 *   R2 (how far)   accelerate        Cross     boost (R1)
 *   L2 (how far)   brake, reverse    Square    look back (L1)
 *   R1             boost             Triangle  camera
 *   L1             next song (L2)    Circle    as it was
 *
 * The race reads how far Cross and Square are pressed, in a narrow band of
 * the DualShock 2's pressure (measured with --press cross@...:PRESSURE): 30
 * of 255 coasts, 38 holds about 58 mph, 44 about 68, 50 and more is full
 * throttle. A trigger's travel is spread over that band, so the whole pull
 * is the throttle, not its first fifth.
 *
 * With TRIGGER EFFECTS, a DualSense's triggers push back while racing: the
 * accelerator lightly, the brake harder from a little way in.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

bool MenuShowing();                         // mouse.cpp
bool ControllerPrompts();                   // prompts.cpp
unsigned long long RaceWorldCalls();        // widescreen.cpp
void F_001A0790(lifted::Context& c);        // the race's HUD

namespace {

// DualShock 2 inputs (libpad's order).
constexpr int kL2 = 8, kR2 = 9, kL1 = 10, kR1 = 11, kTriangle = 12, kCircle = 13, kCross = 14, kSquare = 15;
constexpr u16 Bit(int input) { return u16(1u << input); }
constexpr u16 kRemapped = Bit(kL2) | Bit(kR2) | Bit(kL1) | Bit(kR1) | Bit(kTriangle) | Bit(kCircle) | Bit(kCross) |
                          Bit(kSquare);
// A trigger counts from this far in (of 255): its rest, and a little play.
constexpr u8 kTriggerDeadZone = 12;
// The race's band of pressure (see above), and the pull that is all of it.
constexpr int kBandLow = 24, kBandHigh = 56, kFullPull = 245;

/* A trigger's travel as the pressure the race reads. */
u8 Pressure(u8 travel) {
    if (travel >= kFullPull) return 0xFF;
    const int t = travel - kTriggerDeadZone, span = kFullPull - kTriggerDeadZone;
    return u8(kBandLow + (kBandHigh - kBandLow) * t / span);
}

const bool kLog = std::getenv("PS2HOST_CONTROLS_LOG") != nullptr;
// PS2HOST_MODERN_CONTROLS=1: on, whatever the setting (a scripted run's).
const bool kForced = std::getenv("PS2HOST_MODERN_CONTROLS") != nullptr;

bool g_racing = false;          // modern controls, and racing (ControlsTick)

/* The race's HUD drawn (0x001A0790: only in a race - the menus' and the
   intro's backdrops run the race's world too, without it). */
unsigned long long g_hud_calls = 0;
void RaceHud(lifted::Context& c) {
    ++g_hud_calls;
    F_001A0790(c);
}

/* Racing: the race's world went on, and its HUD was drawn, in the last few
   pad reads (both stop while the game is paused), and no menu shows. */
bool Racing() {
    static unsigned long long world_seen = 0, hud_seen = 0;
    static int world_still = 99, hud_still = 99;
    const unsigned long long world = RaceWorldCalls(), hud = g_hud_calls;
    world_still = world != world_seen ? 0 : world_still + 1;
    hud_still = hud != hud_seen ? 0 : hud_still + 1;
    world_seen = world;
    hud_seen = hud;
    return world_still <= 8 && hud_still <= 8 && !MenuShowing();
}

void Filter(ps2host::HostPadState* pad) {
    if (!g_racing) return;
    const u16 held = pad->buttons;
    u16 out = u16(held & ~kRemapped);
    u8 pressure[16];
    for (int i = 0; i < 16; ++i) pressure[i] = pad->pressure[i];
    // The remapped inputs from nothing: each the most of what is put on it.
    for (int i = 0; i < 16; ++i)
        if (Bit(i) & kRemapped) pressure[i] = 0;
    auto press = [&](int input, u8 how_far) {
        out |= Bit(input);
        if (how_far > pressure[input]) pressure[input] = how_far;
    };
    if (pad->r2 > kTriggerDeadZone) press(kCross, Pressure(pad->r2));
    if (pad->l2 > kTriggerDeadZone) press(kSquare, Pressure(pad->l2));
    if (held & Bit(kCross)) press(kR1, 0xFF);
    if (held & Bit(kR1)) press(kR1, 0xFF);
    if (held & Bit(kSquare)) press(kL1, 0xFF);
    if (held & Bit(kL1)) press(kL2, 0xFF);
    if (held & Bit(kTriangle)) press(kTriangle, 0xFF);
    if (held & Bit(kCircle)) press(kCircle, 0xFF);
    pad->buttons = out;
    for (int i = 0; i < 16; ++i) pad->pressure[i] = pressure[i];
}

}  // namespace

/* Once a pad read (mouse.cpp's MouseTick): whether the race has the modern
   controls now, and the triggers' push for it. */
void ControlsTick() {
    const bool modern = kForced || ps2host::SettingOn("modern_controls", false);
    const bool racing = Racing() && modern;
    if (kLog && racing != g_racing) std::fprintf(stderr, "[controls] %s\n", racing ? "racing: modern" : "the game's");
    g_racing = racing;
    // The triggers' push, while racing with them (a DualSense's).
    ps2host::HostTriggerEffect accelerator, brake;
    if (racing && ps2host::SettingOn("trigger_effects", true)) {
        accelerator.kind = brake.kind = ps2host::HostTriggerEffect::kResistance;
        accelerator.start = 0;
        accelerator.force = 40;
        brake.start = 50;
        brake.force = 170;
    }
    ps2host::HostTriggerEffects(brake, accelerator);
}

/* The race's hints that name the boost button, as the controls have it: R1,
   or Cross - "X" - with the modern controls on a controller. Rewritten in
   place in the language file's strings as they are looked up (the X ones
   are no longer), so either can be found again. `at` a UTF-16 string. */
void RaceHint(u32 at) {
    static const struct { const char* game; const char* modern; } kHints[] = {
        {"PRESS R1 BUTTON TO BOOST", "PRESS X BUTTON TO BOOST"},
        {"WHEN CRASHING HOLD THE R1 BUTTON FOR AFTERTOUCH", "WHEN CRASHING HOLD THE X BUTTON FOR AFTERTOUCH"},
    };
    if (!at || generated::ReadU16(at) != 'P' && generated::ReadU16(at) != 'W') return;
    std::string text;
    for (u32 i = 0; i < 64; ++i) {
        const u16 ch = generated::ReadU16(at + 2 * i);
        if (!ch) break;
        text += char(ch < 128 ? ch : '?');
    }
    const bool modern = (kForced || ps2host::SettingOn("modern_controls", false)) && ControllerPrompts();
    for (const auto& hint : kHints) {
        if (text != hint.game && text != hint.modern) continue;
        const std::string want = modern ? hint.modern : hint.game;
        if (text == want) return;
        u32 i = 0;
        for (; i < want.size(); ++i) generated::Write16(at + 2 * i, u8(want[i]));
        generated::Write16(at + 2 * i, 0);
        return;
    }
}

void RegisterControls() {
    ps2host::SetControllerFilter(Filter);
    ps2host::Override(0x001A0790u, RaceHud, "the race's HUD: racing, for the modern controls");
}
