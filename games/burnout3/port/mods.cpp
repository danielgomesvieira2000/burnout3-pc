/*
 * PC SETTINGS' MODS: Nehalem's PCSX2 patches for Burnout 3 (github.com/
 * Nahelam/PS2-Game-Mods, Burnout 3 Takedown/Various and Crash Breaker
 * Expansion, SLUS-21050), each a setting.
 *
 * Most change the game's code: those functions are lifted again with the
 * mods' words written in (port/tools/mods_spec.py, at build time) and run
 * instead of the originals as the settings say - a change takes effect the
 * next time the function runs. The rest are words of the game's memory the
 * patch writes every frame, written here at 0x001F43B0 (the display's frame
 * step, a few times a frame, racing or not); MIRROR MODE is a byte of the
 * race's, written as it is set up.
 *
 * PS2HOST_MODS_OFF=1: every mod off, those always on too (comparisons).
 */

#include <cstdlib>
#include <cstring>
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
void MusicWatchdog();        // overrides.cpp
void HighFpsFrame();         // high_fps.cpp
int ParticlesLevel();        // graphics.cpp: GRAPHICS' PARTICLES

namespace {

constexpr int kMaxMods = 32;
bool g_on[kMaxMods] = {};
const bool g_all_off = std::getenv("PS2HOST_MODS_OFF") != nullptr;

// No traffic: the race's traffic on or off (0x0055D7FB, set from the event
// as a race loads, 0x001322B0) - off every frame, the next race on again.
constexpr u32 kTraffic = 0x0055D7FBu;
// TRAFFIC's density (0.5X to 3X, "traffic_density"): the traffic's spacing,
// 60.0 in the game's code, read from this word instead (tools/mods_spec.py:
// mod.traffic_density) - 60 / the density, written each frame step before
// the lifted code may read it, so the game's own code runs until then.
constexpr u32 kTrafficSpacing = 0x00000100u;
bool g_spacing_written = false;
float TrafficDensity() {
    const std::string v = ps2host::Setting("traffic_density", "1");
    const float density = std::strtof(v.c_str(), nullptr);
    return density >= 0.25f && density <= 4.0f ? density : 1.0f;
}
// Heavyweights in every mode: the garage's flag for the class (the byte at
// 0x01E8EE94) - set every frame, the game's own back when the mod is turned
// off. The patch writes the word (1): the byte after it is a flag of the
// game's own (0x100 in the word while racing), kept here.
constexpr u32 kHeavyweights = 0x01E8EE94u;
bool g_heavyweights_set = false;
u8 g_heavyweights_was = 0;

bool On(const char* key) { return !g_all_off && ps2host::SettingOn(key, false); }

/* CRASH BREAKER in every mode, Nehalem's Crash Breaker Expansion (its code:
   tools/mods_spec.py). Its pointer: an entry of a table of the game's at
   0x004DDE34 (0x001507D0) made its own code's (0x000B3E20: the game's, then
   its own) - written every frame while the mod is on, the game's back once
   it is turned off. */
constexpr u32 kCrashBreakerEntry = 0x004DDE34u, kCrashBreakerGame = 0x001507D0u, kCrashBreakerMod = 0x000B3E20u;
bool g_crash_breaker_set = false;


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
        // (GRAPHICS' PARTICLES: its level, by the preset unless chosen)
        if (key == "mod.particles_more" || key == "mod.particles_most")
            g_on[i] = !g_all_off && ParticlesLevel() == (key == "mod.particles_more" ? 1 : 2);
        else if (key == "mod.traffic_density")
            g_on[i] = !g_all_off && g_spacing_written && TrafficDensity() != 1.0f;
        else
            g_on[i] = !g_all_off && (key == "always" || ps2host::SettingOn(key, false));
    }
}

/* 0x001F43B0: the display's frame step - the mods' (and GRAPHICS') memory
   written first. */
void Frame(lifted::Context& c) {
    const float spacing = 60.0f / TrafficDensity();
    u32 bits;
    std::memcpy(&bits, &spacing, sizeof bits);
    generated::Write32(kTrafficSpacing, bits);
    g_spacing_written = true;
    Refresh();
    GraphicsFrame();
    CustomSongsFrame();
    MusicWatchdog();
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
    if (On("mod.crash_breaker")) {
        generated::Write32(kCrashBreakerEntry, kCrashBreakerMod);
        g_crash_breaker_set = true;
    } else if (g_crash_breaker_set) {
        generated::Write32(kCrashBreakerEntry, kCrashBreakerGame);
        g_crash_breaker_set = false;
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
