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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "ps2host/disc.h"
#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// mods_lifted.cpp (generated): the changed functions' choosers, the mods' keys
// and the pnach mods' words as memory (place: the pnach's - 0 once at boot,
// 1 every frame, 2 both, 3 once as it is turned on).
void RegisterModFunctions();
extern const char* const kModKeys[];
extern const int kModCount;
struct ModWrite { u32 address, value; u8 size, place; u16 mod; };
extern const ModWrite kModWrites[];
extern const int kModWriteCount;
// The display's frame step (see Frame).
void F_001F43B0(lifted::Context& c);
// GRAPHICS' memory, at the same step (graphics.cpp).
void GraphicsFrame();
void CustomSongsFrame();     // custom_songs.cpp
void MusicWatchdog();        // overrides.cpp
unsigned long long GameLoadSteps();     // overrides.cpp
unsigned long long RaceWorldCalls();    // widescreen.cpp
void HighFpsFrame();         // high_fps.cpp
int ParticlesLevel();        // graphics.cpp: GRAPHICS' PARTICLES

namespace {

constexpr int kMaxMods = 64;
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
// TRAFFIC SERIES as the game started (its files are laid over the disc then,
// TrafficSeriesFiles): its code on or off with them, never changed until the
// next start - a traffic car chosen without its code has no physics. -1
// before the disc is opened.
int g_traffic_series = -1;

/* A race in progress: from its world's first step until the next load (a
   pause does not end it). A pnach mod's code switched mid-race leaves its
   state half done - Demo Crashes turned off mid-crash froze the race - so a
   change to one made then (the F9 panel's) waits for the next load. */
bool RaceInProgress() {
    static unsigned long long world = 0, loads = 0;
    static bool racing = false;
    if (RaceWorldCalls() != world) world = RaceWorldCalls(), racing = true;
    if (GameLoadSteps() != loads) loads = GameLoadSteps(), racing = false;
    return racing;
}

bool Pnach(int mod) {
    static bool pnach[kMaxMods] = {}, known = false;
    if (!known) {
        for (int k = 0; k < kModWriteCount; ++k)
            if (kModWrites[k].mod < kMaxMods) pnach[kModWrites[k].mod] = true;
        known = true;
    }
    return mod >= 0 && mod < kMaxMods && pnach[mod];
}

bool g_refreshed = false;               // (the first, at start, takes every setting)

void Refresh() {
    const bool racing = RaceInProgress() && g_refreshed;
    g_refreshed = true;
    for (int i = 0; i < kModCount && i < kMaxMods; ++i) {
        const std::string key = kModKeys[i];
        if (racing && Pnach(i)) continue;   // (as it was until the next load)
        if (key == "mod.traffic_series") {
            g_on[i] = g_traffic_series == 1;
            continue;
        }
        // (GRAPHICS' PARTICLES: its level, by the preset unless chosen)
        if (key == "mod.particles_more" || key == "mod.particles_most")
            g_on[i] = !g_all_off && ParticlesLevel() == (key == "mod.particles_more" ? 1 : 2);
        else
            g_on[i] = !g_all_off && (key == "always" || ps2host::SettingOn(key, false));
    }
}

/* The pnach mods' words as memory (their own code's data and state, the
   game's data they change, and their code - as PCSX2 has it in memory):
   all of a mod's as it is turned on (the game's own kept), those of place 1
   and 2 each frame while it stays on, the game's own back once it is off -
   and then every mod still on writes its words again, in case two share one. */
struct Written {
    u32 address, value;
    u8 size;
};
std::vector<Written> g_saved[kMaxMods];
bool g_written[kMaxMods] = {};

u32 ReadSized(u32 address, u8 size) {
    return size == 1 ? u32(generated::ReadU8(address)) : size == 2 ? u32(generated::ReadU16(address))
                                                                    : u32(generated::ReadU32(address));
}

void WriteSized(u32 address, u32 value, u8 size) {
    if (size == 1) generated::Write8(address, u8(value));
    else if (size == 2) generated::Write16(address, u16(value));
    else generated::Write32(address, value);
}

/* Each mod's words: their range in kModWrites (it lists them mod by mod),
   and those it writes every frame (place 1 and 2) apart. */
struct ModWords {
    int begin = 0, end = 0;
    std::vector<int> every_frame;
};
std::vector<ModWords>& Words() {
    static std::vector<ModWords> words = [] {
        std::vector<ModWords> w(kMaxMods);
        for (int k = 0; k < kModWriteCount; ++k) {
            const ModWrite& x = kModWrites[k];
            if (x.mod >= kMaxMods) continue;
            ModWords& m = w[x.mod];
            if (m.end == 0) m.begin = k;
            m.end = k + 1;
            if (x.place == 1 || x.place == 2) m.every_frame.push_back(k);
        }
        return w;
    }();
    return words;
}

void ModMemory() {
    std::vector<ModWords>& words = Words();
    // While a pnach mod is on, a read of no memory finds 0 and a write to
    // none goes nowhere, as on PCSX2 (Demo Crashes reads through a pointer
    // of the demo's build): not the run stopped.
    bool any = false;
    for (int m = 0; m < kMaxMods; ++m) any = any || (words[m].end > 0 && g_on[m]);
    static bool ignoring = false;
    if (any != ignoring) {
        ps2host::SetUnmappedIgnored(any);
        ignoring = any;
    }
    // A frame's words once a blank (the frame step runs a few times in one),
    // as PCSX2 applies them each vertical blank.
    static u64 last_blank = ~0ull;
    const u64 blank = ps2host::FrameNumber();
    const bool new_blank = blank != last_blank;
    last_blank = blank;
    bool removed = false;
    for (int m = 0; m < kMaxMods; ++m) {
        const ModWords& w = words[m];
        if (w.end == 0) continue;
        if (g_on[m] && !g_written[m]) {
            for (int k = w.begin; k < w.end; ++k) {
                const ModWrite& x = kModWrites[k];
                if (x.mod != m) continue;
                g_saved[m].push_back({x.address, ReadSized(x.address, x.size), x.size});
                WriteSized(x.address, x.value, x.size);
            }
            g_written[m] = true;
        } else if (g_on[m] && new_blank) {
            for (int k : w.every_frame) WriteSized(kModWrites[k].address, kModWrites[k].value, kModWrites[k].size);
        } else if (!g_on[m] && g_written[m]) {
            for (auto it = g_saved[m].rbegin(); it != g_saved[m].rend(); ++it) WriteSized(it->address, it->value, it->size);
            g_saved[m].clear();
            g_written[m] = false;
            removed = true;
        }
    }
    // A mod turned off may have shared words with one still on: theirs again.
    if (removed)
        for (int m = 0; m < kMaxMods; ++m)
            if (g_on[m] && g_written[m])
                for (int k = words[m].begin; k < words[m].end; ++k)
                    if (kModWrites[k].mod == m) WriteSized(kModWrites[k].address, kModWrites[k].value, kModWrites[k].size);
}

/* 0x001F43B0: the display's frame step - the mods' (and GRAPHICS') memory
   written first. */
void Frame(lifted::Context& c) {
    Refresh();
    ModMemory();
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

/* Whether a mod of that key was built in (a pnach's: its file was there). */
bool ModBuilt(const char* key) {
    for (int i = 0; i < kModCount; ++i)
        if (std::string(kModKeys[i]) == key) return true;
    return false;
}

/* Whether the mod at `mod` (kModKeys' order) is on (mods_lifted.cpp asks):
   a pnach mod's code only once its words are in memory (the frame step's
   ModMemory) - on from the settings at start, the boot runs before the
   first frame step, and Demo Crashes' code without its data wrote past the
   end of RAM. */
bool ModOn(int mod) { return mod >= 0 && mod < kMaxMods && g_on[mod] && (g_written[mod] || !Pnach(mod)); }

/* TRAFFIC SERIES (bryan's): its cars' files - pveh/TRAF, a new pveh/vlist.bin
   and Data/Globalus.bin - laid over the disc (the engine's Disc::AddFiles)
   when the game starts with it on; its code is a mod as the rest. */
void TrafficSeriesFiles() {
    g_traffic_series = 0;
    if (g_all_off || !ps2host::SettingOn("mod.traffic_series", false)) return;
    // The mods' folder: PS2_MODS_DIR, else "b3 mod pnachs" beside the
    // executable or in a folder above it (the repository's root).
    namespace fs = std::filesystem;
    std::error_code error;
    fs::path folder;
    if (const char* dir = std::getenv("PS2_MODS_DIR"); dir && *dir) folder = fs::u8path(dir);
    std::vector<fs::path> starts{fs::current_path(error)};
    if (!ps2host::SettingsPath().empty())
        starts.insert(starts.begin(), fs::absolute(fs::u8path(ps2host::SettingsPath()), error).parent_path());
    for (const fs::path& start : starts)
        for (fs::path at = start; folder.empty() && !at.empty(); at = at.parent_path()) {
            if (fs::is_directory(at / "b3 mod pnachs", error)) folder = at / "b3 mod pnachs";
            if (at == at.parent_path()) break;
        }
    if (folder.empty()) {
        std::fprintf(stderr, "[mods] TRAFFIC SERIES: no \"b3 mod pnachs\" folder found for its files\n");
        return;
    }
    ps2host::GuestDisc().AddFiles((folder / "BurnoutTrafficSeries" / "game_files").u8string());
    g_traffic_series = 1;
}

void RegisterMods() {
    Refresh();
    ps2host::Disc::OnOpen(TrafficSeriesFiles);
    RegisterModFunctions();
    ps2host::Override(0x001F43B0u, Frame, "the display's frame step: MODS' memory (PC SETTINGS)");
}
