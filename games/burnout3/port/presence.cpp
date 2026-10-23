/*
 * Discord's Rich Presence (engine: ps2host/presence.h): what the player is
 * doing, on their Discord profile - "Raging in Silver Lake", the track's
 * whole name under it, the time since the event began; "In the menus"
 * otherwise. PC SETTINGS > ONLINE > DISCORD STATUS turns it off.
 *
 * What the game knows (found with --watch and the program's tables):
 *   - the event: the race's holder at 0x0051BA88 (0x0051AD00, splitscreen's
 *     0x0051AEC0), its event object at +0x1B4; the object's class (its
 *     vtable) says its kind at +0x98, a method answering a constant (jr ra;
 *     addiu v0, zero, N): 0 a race, 3 a Road Rage, 6 a Crash - 1, 4 and 7
 *     the other kinds of event, not told apart yet.
 *   - the track: its folder's name ("us_c1_v1") as a 64-bit hash at
 *     0x0064D370 (0x0021AEF0, 40 to a character); the program's 36 tracks'
 *     hashes at 0x004CC460 and, after them at 0x004CC580, each one's name
 *     (a string's id: "SILVER LAKE SOUTHBOUND").
 *   - a string: 0x00271A00's table, ReadU32(ReadU32(ReadU32(0x0051BA44) +
 *     0xC) + id * 4), UTF-16 - the language file's own words, read as the
 *     game runs (none of them kept here).
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps2host/host.h"
#include "ps2host/presence.h"
#include "ps2host/settings.h"

namespace presence {
namespace {

// Discord's application (its developer portal): the name the profile shows
// ("Playing Burnout 3: Takedown") and the picture's asset.
constexpr const char* kApplicationId = "";
constexpr const char* kLargeImage = "burnout3";
constexpr const char* kLargeText = "Burnout 3: Takedown - the PC port";
constexpr const char* kButtonLabel = "Get the PC port";
constexpr const char* kButtonUrl = "https://github.com/danielgomesvieira2000/burnout3-pc";

constexpr u32 kHolder = 0x0051BA88u, kSplitHolder = 0x0051AEC0u;
constexpr u32 kTrackHash = 0x0064D370u;
constexpr u32 kTrackHashes = 0x004CC460u, kTrackNames = 0x004CC580u, kTrackCount = 36;
constexpr u32 kStrings = 0x0051BA44u;
constexpr u32 kRam = 0x02000000u;

const bool kLog = std::getenv("B3_PRESENCE_LOG") != nullptr;

bool InRam(u32 a) { return a >= 0x00100000u && a < kRam && (a & 3u) == 0; }

/* The event's kind (its class's +0x98), or -1. */
int EventKind() {
    const u32 holder = u32(generated::ReadU32(kHolder));
    if (!InRam(holder)) return -1;
    const u32 event = u32(generated::ReadU32(holder + 0x1B4u));
    if (!InRam(event)) return -1;
    const u32 vtable = u32(generated::ReadU32(event));
    if (!InRam(vtable)) return -1;
    const u32 method = u32(generated::ReadU32(vtable + 0x98u));
    if (!InRam(method) || u32(generated::ReadU32(method)) != 0x03E00008u) return -1;   // jr ra
    const u32 slot = u32(generated::ReadU32(method + 4u));
    if ((slot >> 16) == 0x2402u) return int(slot & 0xFFFFu);                          // addiu v0, zero, N
    if (slot == 0x0000102Du) return 0;                                                  // daddu v0, zero, zero
    return -1;
}

bool Splitscreen() { return u32(generated::ReadU32(kHolder)) == kSplitHolder; }

/* The game's string `id`, as written there (UTF-16, these all ASCII). */
std::string GameString(u32 id) {
    const u32 holder = u32(generated::ReadU32(kStrings));
    if (!InRam(holder)) return {};
    const u32 table = u32(generated::ReadU32(holder + 0xCu));
    if (!InRam(table)) return {};
    const u32 text = u32(generated::ReadU32(table + id * 4u));
    if (text < 0x00100000u || text >= kRam) return {};
    std::string s;
    for (u32 i = 0; i < 64; ++i) {
        const u16 ch = u16(generated::ReadU16(text + i * 2u));
        if (!ch) break;
        s += ch < 0x80 ? char(ch) : '?';
    }
    return s;
}

/* "KINGS OF THE ROAD" as "Kings of the Road". */
std::string Titled(const std::string& upper) {
    std::string out, word;
    auto flush = [&] {
        if (word.empty()) return;
        const bool small = !out.empty() && (word == "OF" || word == "THE" || word == "AND");
        for (std::size_t i = 0; i < word.size(); ++i)
            if (word[i] >= 'A' && word[i] <= 'Z' && (i > 0 || small)) word[i] = char(word[i] - 'A' + 'a');
        out += word;
        word.clear();
    };
    for (char ch : upper) {
        if (ch == ' ' || ch == '-') {
            flush();
            out += ch;
        } else {
            word += ch;
        }
    }
    flush();
    return out;
}

/* The track's whole name ("Silver Lake Southbound"), "" when it is none of
   the program's. */
std::string TrackName() {
    const u64 hash = u64(u32(generated::ReadU32(kTrackHash))) | u64(u32(generated::ReadU32(kTrackHash + 4u))) << 32;
    if (!hash) return {};
    for (u32 i = 0; i < kTrackCount; ++i) {
        const u64 h = u64(u32(generated::ReadU32(kTrackHashes + i * 8u))) |
                      u64(u32(generated::ReadU32(kTrackHashes + i * 8u + 4u))) << 32;
        if (h == hash) return Titled(GameString(u32(generated::ReadU32(kTrackNames + i * 4u))));
    }
    return {};
}

/* "Silver Lake Southbound" as the place, "Silver Lake". */
std::string Place(const std::string& track) {
    const std::size_t space = track.find_last_of(' ');
    if (space != std::string::npos && track.size() > 5 && track.compare(track.size() - 5, 5, "bound") == 0)
        return track.substr(0, space);
    return track;
}

/* The first line: what the player is up to, by the event's kind. */
std::string Doing(int kind, const std::string& place) {
    const bool at = !place.empty();
    switch (kind) {
        case 0: return at ? "Racing through " + place : "Racing";
        case 3: return at ? "Raging in " + place : "On a Road Rage";
        case 6: return at ? "Causing mayhem in " + place : "Causing mayhem";
        default: return at ? "Burning rubber in " + place : "Burning rubber";
    }
}

struct State {
    bool on = false;                // DISCORD STATUS
    unsigned ticks = 0;
    unsigned not_racing = 9999;     // pad reads since the last racing one
    bool in_event = false;
    int kind = -1;
    std::string track;
    long long started = 0;
};
State g;

}  // namespace

/* Once a pad read (controls.cpp's ControlsTick). */
void Tick(bool racing) {
    // The setting, every second or so (it is the player's to change).
    if (g.ticks++ % 60 == 0) {
        const bool on = ps2host::SettingOn("discord", true);
        if (on != g.on) {
            g.on = on;
            const char* env = std::getenv("PS2HOST_DISCORD_APP_ID");
            const std::string id = env && *env ? env : ps2host::Setting("discord_app_id", kApplicationId);
            if (on && !id.empty()) {
                ps2host::PresenceStart(id);
            } else {
                ps2host::PresenceStop();
            }
            g.in_event = false;
            g.not_racing = 9999;
        }
    }
    if (!g.on || !ps2host::PresenceRunning()) return;

    g.not_racing = racing ? 0 : g.not_racing + 1;
    ps2host::PresenceActivity a;
    a.large_image = kLargeImage;
    a.large_text = kLargeText;
    a.button_label = kButtonLabel;
    a.button_url = kButtonUrl;

    // Paused, or a moment between (a restart): still the event, for a while.
    if (g.not_racing < 60 * 20) {
        if (racing) {
            const int kind = EventKind();
            const std::string track = TrackName();
            // A new event: its clock from now (the same one again after a
            // pause or a restart keeps it).
            if (!g.in_event || kind != g.kind || track != g.track) {
                g.kind = kind;
                g.track = track;
                g.started = ps2host::PresenceNow();
                if (kLog) std::fprintf(stderr, "[presence] event kind %d, track \"%s\"\n", kind, track.c_str());
            }
            g.in_event = true;
        }
        if (g.in_event) {
            a.details = Doing(g.kind, Place(g.track));
            a.state = g.track;
            if (Splitscreen()) a.state = a.state.empty() ? "Splitscreen" : a.state + " - splitscreen";
            if (!racing) a.state = a.state.empty() ? "Paused" : a.state + " (paused)";
            a.start = g.started;
            ps2host::PresenceSet(a);
            return;
        }
    }
    g.in_event = false;
    a.details = "In the menus";
    ps2host::PresenceSet(a);
}

}  // namespace presence
