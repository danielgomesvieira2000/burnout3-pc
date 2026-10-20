/*
 * PC SETTINGS' MODS: Nehalem's PCSX2 patches for Burnout 3 (github.com/
 * Nahelam/PS2-Game-Mods, Burnout 3 Takedown/Various, SLUS-21050), each a
 * setting.
 *
 * Most change the game's code: those functions are lifted again with the
 * mods' words written in (port/tools/lift_mods.py, at build time) and run
 * instead of the originals as the settings say - a change takes effect the
 * next time the function runs. The rest are words of the game's memory the
 * patch writes every frame, written here at 0x001F43B0 (the display's frame
 * step, a few times a frame, racing or not); MIRROR MODE is a byte of the
 * race's, written as it is set up.
 *
 * PS2HOST_MODS_OFF=1: every mod off, those always on too (comparisons).
 */

#include <cstdlib>
#include <string>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// mods_lifted.cpp (generated): the changed functions' choosers, the mods' keys.
void RegisterModFunctions();
extern const char* const kModKeys[];
extern const int kModCount;
// The display's frame step (see Frame).
void F_001F43B0(lifted::Context& c);
// GRAPHICS' memory, at the same step (graphics.cpp).
void GraphicsFrame();
void CustomSongsFrame();     // custom_songs.cpp
void HighFpsFrame();         // high_fps.cpp

namespace {

constexpr int kMaxMods = 32;
bool g_on[kMaxMods] = {};
const bool g_all_off = std::getenv("PS2HOST_MODS_OFF") != nullptr;

// No traffic: the race's traffic on or off (0x0055D7FB, set from the event
// as a race loads, 0x001322B0) - off every frame, the next race on again.
constexpr u32 kTraffic = 0x0055D7FBu;
// Heavyweights in every mode: the garage's flag for the class (the byte at
// 0x01E8EE94) - set every frame, the game's own back when the mod is turned
// off. The patch writes the word (1): the byte after it is a flag of the
// game's own (0x100 in the word while racing), kept here.
constexpr u32 kHeavyweights = 0x01E8EE94u;
bool g_heavyweights_set = false;
u8 g_heavyweights_was = 0;

bool On(const char* key) { return !g_all_off && ps2host::SettingOn(key, false); }

/* The game's one race (0x00665EC0), set up once at boot (0x0019EE40): its
   +0xC (0, the view as the track is) 1 for MIRROR MODE - everything
   mirrored, the controls as they were - and its +0xF (0, a splitscreen
   race's views one above the other) 1 for SPLITSCREEN VERTICAL, side by
   side. The patches (0x0019EFA4 and 0x0019EFAC, sb s2) store their caller's
   s2 in the set-up, whatever that holds: here a 1 each frame while the
   setting is on (it changes in PC SETTINGS long after the set-up), the
   game's 0 back once when it is turned off. */
constexpr u32 kRace = 0x00665EC0u;
struct RaceFlag {
    const char* key;
    u32 offset;
    bool set = false;
};
RaceFlag g_race_flags[] = {{"mod.mirror", 0xCu}, {"mod.vertical_split", 0xFu}};

/* The settings as they are now. */
void Refresh() {
    for (int i = 0; i < kModCount && i < kMaxMods; ++i) {
        const std::string key = kModKeys[i];
        g_on[i] = !g_all_off && (key == "always" || ps2host::SettingOn(key, false));
    }
}

/* 0x001F43B0: the display's frame step - the mods' (and GRAPHICS') memory
   written first. */
void Frame(lifted::Context& c) {
    Refresh();
    GraphicsFrame();
    CustomSongsFrame();
    HighFpsFrame();
    if (On("mod.no_traffic")) generated::Write8(kTraffic, 0);
    if (On("mod.heavyweights")) {
        if (!g_heavyweights_set) g_heavyweights_was = u8(generated::ReadU8(kHeavyweights));
        g_heavyweights_set = true;
        generated::Write8(kHeavyweights, 1);
    } else if (g_heavyweights_set) {
        g_heavyweights_set = false;
        generated::Write8(kHeavyweights, g_heavyweights_was);
    }
    for (RaceFlag& f : g_race_flags) {
        if (On(f.key)) {
            generated::Write8(kRace + f.offset, 1);
            f.set = true;
        } else if (f.set) {
            generated::Write8(kRace + f.offset, 0);
            f.set = false;
        }
    }
    F_001F43B0(c);
}

}  // namespace

/* Whether the mod at `mod` (kModKeys' order) is on (mods_lifted.cpp asks). */
bool ModOn(int mod) { return mod >= 0 && mod < kMaxMods && g_on[mod]; }

void RegisterMods() {
    Refresh();
    RegisterModFunctions();
    ps2host::Override(0x001F43B0u, Frame, "the display's frame step: MODS' memory (PC SETTINGS)");
}
