/*
 * Custom songs in EA Trax: MP3s dropped into the `custom_songs` folder beside
 * the game join its 44 songs - in the EA TRAX menu, with the same ALL / RACE
 * ONLY / MENU ONLY / OFF choice, and in the rotation (random or in order, the
 * song's banner, L2 to skip). docs/CUSTOM_SONGS_PLAN.md has how the game's
 * EA Trax works; in short:
 *
 * - The list. The game's table of songs (0x004A5600, 24 bytes a song: index,
 *   0, title, album, artist - string IDs - and the choice's bits) is copied
 *   into a larger one with the custom songs after it, in memory nothing uses
 *   (kArea, below the executable), and the descriptor (0x004A5A20: +4 the
 *   count, +0x4C the table) points the player and the menu to it. Their names
 *   are string IDs of our own (kStringBase on), answered by the string
 *   lookup (0x00271A60).
 * - The save. The memory card keeps a byte a song for 51 at most, so the
 *   game's 44 are kept there as ever and the custom songs' choices in
 *   custom_songs/settings.ini: the four places the game copies the choices
 *   to or from the card (0x0028B220, 0x0028B380, the menu's Confirm and Back,
 *   0x004313E0) take the descriptor's copy (0x003FBB50) with the count cut to
 *   44, and the host does the rest.
 * - Playing. A custom song's stream is a stand-in (kFakeStream): the stream
 *   calls the player makes for it (RenderWare Audio's, 0x00385EB0-0x00386B30)
 *   are answered here - open, play, volume, position, stop - the MP3 played
 *   by the host (ps2host/music.h) - and the stream's state word (+0x1D0: 6
 *   playing, 4 ended) kept as RenderWare Audio keeps it, for the calls that
 *   only read it. The player's own code runs as for its own songs.
 * - Past 51 songs. The random order is a byte array inside the player
 *   (+0x0D, room for 51): with more songs, the two functions that use it
 *   (0x003FB8B0, 0x003FBDB0) run as here, the order in an array of ours.
 *
 * With no MP3 in the folder nothing changes: every override passes the call
 * through.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/music.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "mods.h"

void F_003FCD20(lifted::Context& c);
void F_003FCC10(lifted::Context& c);
void F_003FBB50(lifted::Context& c);
void F_003FB8B0(lifted::Context& c);
void F_003FBDB0(lifted::Context& c);
void F_003FC8C0(lifted::Context& c);
void F_00271A60(lifted::Context& c);
void F_00386B30(lifted::Context& c);
void F_003865A0(lifted::Context& c);
void F_00386290(lifted::Context& c);
void F_00386300(lifted::Context& c);
void F_00386240(lifted::Context& c);
void F_003863C0(lifted::Context& c);
void F_00386A60(lifted::Context& c);
void F_00386080(lifted::Context& c);
void F_00386020(lifted::Context& c);

namespace {

namespace fs = std::filesystem;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

// The game's.
constexpr u32 kGameTable = 0x004A5600u;             // 44 x 24 bytes
constexpr u32 kDescriptor = 0x004A5A20u;            // +4 count, +0x4C table
constexpr u32 kGameSongs = 44;
constexpr u32 kEntry = 24;
constexpr u32 kChoice = 0x14;                       // an entry's choice bits
constexpr u32 kProfileSongs = 0x004F5040u;          // the card's byte a song
constexpr u32 kInOrderMax = 51;                     // the player's own random order's room
// The player object's fields (0x003FCD20 copies the descriptor into it).
constexpr u32 kCount = 0x6C, kTable = 0xB4, kCurrent = 0xD8, kState = 0xC8, kStream = 0xC0;
// RenderWare Audio's stream: its state word.
constexpr u32 kStreamState = 0x1D0;
enum : u32 { kStreamPlaying = 6, kStreamEnded = 4, kStreamStopped = 5, kStreamIdle = 0, kStreamOpen = 3 };

// Ours: memory below the executable (0x00100000) the game never touches -
// checked empty before it is used.
constexpr u32 kArea = 0x000E0000u, kAreaEnd = 0x00100000u;
constexpr u32 kFakeStream = kArea;                  // a stand-in stream object (0x400 bytes)
constexpr u32 kTable0 = kArea + 0x400;              // the larger table (256 x 24)
constexpr u32 kStrings = kArea + 0x2000;            // the custom songs' names
constexpr u32 kNameChars = 64;                      // a name's room, terminator included
constexpr u32 kMaxCustom = 100;
constexpr u32 kStringBase = 0x7000;                 // string IDs: kStringBase + 3k + {title, album, artist}
constexpr u32 kShownChars = 36;                     // what the menu's line and the banner have room for
constexpr u32 kDefaultChoice = 15;                  // as the game's own: ALL

// The player's calls that make its stream (state 0 and 5 of 0x003FC8C0).
constexpr u32 kCreateFromPlayer[] = {0x003FC92Cu, 0x003FCB68u};
// The descriptor copies that go with the card's bytes: 0x0028B220 and
// 0x0028B380 apply them, the menu's Back (0x0043151C) puts them back, its
// Confirm (0x00431458) keeps the choices.
constexpr u32 kApplyCopies[] = {0x0028B324u, 0x0028B504u, 0x0043151Cu};
constexpr u32 kKeepCopy = 0x00431458u;

// The game's music level against an MP3's (mastered louder than the game's
// streams were).
constexpr float kLevel = 1.0f;

struct Song {
    fs::path file;
    std::u16string title, artist;
};

struct State {
    bool scanned = false, installed = false, area_ok = true;
    std::vector<Song> songs;
    std::map<std::u16string, u32> choices;          // by file name
    u32 player = 0;
    // The stand-in stream: in use, and the song it plays.
    bool fake_bound = false;
    u32 fake_song = 0;
    // The random order past kInOrderMax songs.
    u8 order[256] = {};
    double next_scan = 0;
};

State& St() {
    static State s;
    return s;
}

fs::path Folder() {
    if (const char* dir = std::getenv("PS2HOST_CUSTOM_SONGS")) return fs::path(dir);
    const std::string settings = ps2host::SettingsPath();
    return (settings.empty() ? fs::current_path() : fs::path(settings).parent_path()) / "custom_songs";
}

// ------------------------------------------------------------ names

/* A character the game's font has (ASCII), accented Latin letters as their
   plain ones; others dropped. */
char16_t Plain(wchar_t ch) {
    if (ch >= 0x20 && ch < 0x7F) return char16_t(ch);
    static const struct { wchar_t from, to; } kFold[] = {
        {L'À', 'A'}, {L'Á', 'A'}, {L'Â', 'A'}, {L'Ã', 'A'}, {L'Ä', 'A'}, {L'Å', 'A'},
        {L'Ç', 'C'}, {L'È', 'E'}, {L'É', 'E'}, {L'Ê', 'E'}, {L'Ë', 'E'}, {L'Ì', 'I'},
        {L'Í', 'I'}, {L'Î', 'I'}, {L'Ï', 'I'}, {L'Ñ', 'N'}, {L'Ò', 'O'}, {L'Ó', 'O'},
        {L'Ô', 'O'}, {L'Õ', 'O'}, {L'Ö', 'O'}, {L'Ø', 'O'}, {L'Ù', 'U'}, {L'Ú', 'U'},
        {L'Û', 'U'}, {L'Ü', 'U'}, {L'Ý', 'Y'}, {L'ß', 's'}, {L'à', 'a'}, {L'á', 'a'},
        {L'â', 'a'}, {L'ã', 'a'}, {L'ä', 'a'}, {L'å', 'a'}, {L'ç', 'c'}, {L'è', 'e'},
        {L'é', 'e'}, {L'ê', 'e'}, {L'ë', 'e'}, {L'ì', 'i'}, {L'í', 'i'}, {L'î', 'i'},
        {L'ï', 'i'}, {L'ñ', 'n'}, {L'ò', 'o'}, {L'ó', 'o'}, {L'ô', 'o'}, {L'õ', 'o'},
        {L'ö', 'o'}, {L'ø', 'o'}, {L'ù', 'u'}, {L'ú', 'u'}, {L'û', 'u'}, {L'ü', 'u'},
        {L'ý', 'y'}, {L'ÿ', 'y'}, {L'‘', '\''}, {L'’', '\''}, {L'“', '"'}, {L'”', '"'},
        {L'–', '-'}, {L'—', '-'}};
    for (const auto& f : kFold)
        if (f.from == ch) return char16_t(f.to);
    return 0;
}

std::u16string Shown(const std::wstring& text) {
    std::u16string out;
    for (wchar_t ch : text)
        if (char16_t p = Plain(ch)) out += p;
    // Spaces at either end, and runs of them, as one.
    std::u16string squeezed;
    for (char16_t ch : out)
        if (!(ch == u' ' && (squeezed.empty() || squeezed.back() == u' '))) squeezed += ch;
    while (!squeezed.empty() && squeezed.back() == u' ') squeezed.pop_back();
    if (squeezed.size() > kShownChars) {
        squeezed.resize(kShownChars - 3);
        while (!squeezed.empty() && squeezed.back() == u' ') squeezed.pop_back();
        squeezed += u"...";
    }
    return squeezed;
}

/* "Artist - Title.mp3": the artist before the first " - ", the title after;
   without one, the whole name is the title. */
Song Named(const fs::path& file) {
    const std::wstring stem = file.stem().wstring();
    Song s;
    s.file = file;
    const std::size_t dash = stem.find(L" - ");
    if (dash == std::wstring::npos) {
        s.title = Shown(stem);
        s.artist = u"Unknown Artist";
    } else {
        s.artist = Shown(stem.substr(0, dash));
        s.title = Shown(stem.substr(dash + 3));
        if (s.artist.empty()) s.artist = u"Unknown Artist";
    }
    if (s.title.empty()) s.title = u"Untitled";
    return s;
}

std::vector<Song> Scan() {
    std::vector<Song> songs;
    std::error_code error;
    const fs::path folder = Folder();
    if (!fs::is_directory(folder, error)) return songs;
    for (const auto& item : fs::directory_iterator(folder, error)) {
        if (!item.is_regular_file(error)) continue;
        std::wstring ext = item.path().extension().wstring();
        for (wchar_t& ch : ext) ch = wchar_t(std::towlower(ch));
        if (ext == L".mp3") songs.push_back(Named(item.path()));
    }
    std::sort(songs.begin(), songs.end(), [](const Song& a, const Song& b) {
        std::wstring x = a.file.filename().wstring(), y = b.file.filename().wstring();
        for (wchar_t& ch : x) ch = wchar_t(std::towlower(ch));
        for (wchar_t& ch : y) ch = wchar_t(std::towlower(ch));
        return x < y;
    });
    if (songs.size() > kMaxCustom) {
        std::fprintf(stderr, "[custom songs] %zu MP3s: the first %u by name are played\n", songs.size(), kMaxCustom);
        songs.resize(kMaxCustom);
    }
    return songs;
}

// ------------------------------------------------------------ the choices file

fs::path ChoicesFile() { return Folder() / "settings.ini"; }

/* Lines "<choice> <file name>": the menu's choice for each custom song. */
void LoadChoices() {
    State& st = St();
    st.choices.clear();
    std::ifstream in(ChoicesFile());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const u32 choice = u32(std::strtoul(line.substr(0, space).c_str(), nullptr, 10)) & 15u;
        const fs::path name = fs::u8path(line.substr(space + 1));
        st.choices[name.u16string()] = choice;
    }
}

void SaveChoices() {
    State& st = St();
    std::ofstream out(ChoicesFile(), std::ios::trunc);
    if (!out) return;
    out << "# The EA TRAX menu's choice for each custom song (15 ALL, 1 RACE ONLY, 4 MENU ONLY, 0 OFF)\n";
    for (const auto& [name, choice] : st.choices) out << choice << ' ' << fs::path(name).u8string() << '\n';
}

u32 ChoiceOf(const Song& s) {
    const auto found = St().choices.find(s.file.filename().u16string());
    return found == St().choices.end() ? kDefaultChoice : found->second;
}

// ------------------------------------------------------------ the list

bool AreaEmpty() {
    for (u32 a = kArea; a < kAreaEnd; a += 4)
        if (generated::ReadU32(a)) return false;
    return true;
}

void WriteName(u32 id, const std::u16string& text) {
    const u32 k = id - kStringBase;
    const u32 at = kStrings + k * kNameChars * 2;
    u32 i = 0;
    for (; i < text.size() && i + 1 < kNameChars; ++i) generated::Write16(at + 2 * i, text[i]);
    generated::Write16(at + 2 * i, 0);
}

/* The table, the names and the descriptor, for the songs found; the first
   44 entries (the game's, their choices) from the table in use. */
void Build(const std::vector<Song>& songs) {
    State& st = St();
    const u32 from = st.installed ? kTable0 : kGameTable;
    if (!st.installed) {
        if (!AreaEmpty()) {
            std::fprintf(stderr, "[custom songs] memory 0x%06X-0x%06X is in use: no custom songs\n", kArea, kAreaEnd);
            st.area_ok = false;
            return;
        }
        LoadChoices();
    }
    // The game's 44 as they are (copied before the custom entries change).
    std::vector<u32> game(kGameSongs * kEntry / 4);
    for (u32 i = 0; i < game.size(); ++i) game[i] = generated::ReadU32(from + 4 * i);
    for (u32 i = 0; i < game.size(); ++i) generated::Write32(kTable0 + 4 * i, game[i]);
    for (u32 k = 0; k < songs.size(); ++k) {
        const u32 entry = kTable0 + (kGameSongs + k) * kEntry, id = kStringBase + 3 * k;
        generated::Write32(entry + 0x0, kGameSongs + k);
        generated::Write32(entry + 0x4, 0);
        generated::Write32(entry + 0x8, id);
        generated::Write32(entry + 0xC, id + 1);
        generated::Write32(entry + 0x10, id + 2);
        generated::Write32(entry + kChoice, ChoiceOf(songs[k]));
        WriteName(id, songs[k].title);
        WriteName(id + 1, u"Custom Songs");
        WriteName(id + 2, songs[k].artist);
    }
    const u32 count = kGameSongs + u32(songs.size());
    generated::Write32(kDescriptor + 4, count);
    generated::Write32(kDescriptor + 0x4C, kTable0);
    if (st.player) {
        generated::Write32(st.player + kCount, count);
        generated::Write32(st.player + kTable, kTable0);
    }
    st.songs = songs;
    if (!st.installed)
        std::fprintf(stderr, "[custom songs] %zu custom song%s in EA Trax (%s)\n", songs.size(),
                     songs.size() == 1 ? "" : "s", Folder().u8string().c_str());
    st.installed = true;
}

/* The folder made where it is missing (beside the game; not in a scripted
   run), with how to name the songs in it. */
void MakeFolder() {
    if (ps2host::SettingsPath().empty() || ps2host::VirtualClock() || std::getenv("PS2HOST_CUSTOM_SONGS")) return;
    std::error_code error;
    const fs::path folder = Folder();
    if (fs::exists(folder, error)) return;
    if (!fs::create_directories(folder, error)) return;
    std::ofstream readme(folder / "README.txt");
    readme << "Put your own MP3s here: they join the EA TRAX songs (DRIVER DETAILS, SETTINGS,\r\n"
              "EA GAMES TRAX) and play with the game's music.\r\n\r\n"
              "Name each file \"Artist - Song Title.mp3\":\r\n\r\n"
              "    Fuzzeke - Without A Care.mp3      Fuzzeke / Without A Care\r\n"
              "    My Song.mp3                       Unknown Artist / My Song\r\n\r\n"
              "Names longer than 36 characters are cut to fit the game's screens. Songs added\r\n"
              "while the game runs show up within a few seconds. Up to 100 songs.\r\n";
}

/* Found songs, the first time (or none: nothing changes). */
void Install() {
    State& st = St();
    if (st.scanned) return;
    st.scanned = true;
    MakeFolder();
    const std::vector<Song> songs = Scan();
    if (!songs.empty()) Build(songs);
}

bool Custom(u32 song) {
    const State& st = St();
    return st.installed && song >= kGameSongs && song < kGameSongs + st.songs.size();
}

// ------------------------------------------------------------ the choices

/* The custom songs' choices into the table (from the file). */
void ApplyChoices() {
    State& st = St();
    for (u32 k = 0; k < st.songs.size(); ++k)
        generated::Write32(kTable0 + (kGameSongs + k) * kEntry + kChoice, ChoiceOf(st.songs[k]));
}

/* The custom songs' choices from the table (the menu's Confirm) into the file. */
void KeepChoices() {
    State& st = St();
    for (u32 k = 0; k < st.songs.size(); ++k)
        st.choices[st.songs[k].file.filename().u16string()] =
            generated::ReadU32(kTable0 + (kGameSongs + k) * kEntry + kChoice) & 15u;
    SaveChoices();
}

// ------------------------------------------------------------ overrides: the list

/* 0x003FCD20: the player made - the list in place first, the player taking
   the descriptor's count and table. */
void PlayerInit(lifted::Context& c) {
    St().player = u32(c.r[4]);
    Install();
    F_003FCD20(c);
}

/* 0x003FCC10: the card's choices into the game's table (its own address, 44
   of them) - and from there into ours. */
void CardChoices(lifted::Context& c) {
    F_003FCC10(c);
    if (!St().installed) return;
    for (u32 i = 0; i < kGameSongs; ++i)
        generated::Write32(kTable0 + i * kEntry + kChoice, generated::ReadU32(kGameTable + i * kEntry + kChoice));
    ApplyChoices();
}

/* 0x003FBB50: the descriptor copied (a1 <= 0) - for the card's bytes, 44. */
void DescriptorCopy(lifted::Context& c) {
    const u32 ra = u32(c.r[31]), to = u32(c.r[6]);
    const bool global = s32(u32(c.r[5])) <= 0;
    F_003FBB50(c);
    if (!St().installed || !global) return;
    if (std::find(std::begin(kApplyCopies), std::end(kApplyCopies), ra) != std::end(kApplyCopies)) {
        generated::Write32(to + 4, std::min(u32(generated::ReadU32(to + 4)), kGameSongs));
        ApplyChoices();
    } else if (ra == kKeepCopy) {
        generated::Write32(to + 4, std::min(u32(generated::ReadU32(to + 4)), kGameSongs));
        KeepChoices();
    }
}

/* 0x00271A60: a string by ID - the custom songs' names ours. */
void StringLookup(lifted::Context& c) {
    const u32 id = u32(c.r[5]);
    if (St().installed && id >= kStringBase && id < kStringBase + 3 * St().songs.size()) {
        c.r[2] = kStrings + (id - kStringBase) * kNameChars * 2;
        return;
    }
    Modded<0x00271A60u, F_00271A60>(c);
}

// ------------------------------------------------------------ overrides: the order past 51

/* The game's random numbers (two words at gp-0x57F8). */
u32 Random(lifted::Context& c) {
    const u32 g = u32(c.r[28]) - 0x57F8u;
    const u32 a = generated::ReadU32(g), b = generated::ReadU32(g + 4);
    const u32 x = (a << 16) + u32(s32(a) >> 16) + b;
    generated::Write32(g, x);
    generated::Write32(g + 4, b + x);
    return x;
}

/* The order dealt again for `count` songs, as both functions do it. */
void Reshuffle(lifted::Context& c, u32 p, u32 count) {
    u8* order = St().order;
    generated::Write32(p + 0x44, count);
    for (s32 i = 0; i < s32(count); ++i) order[i] = u8(i);
    for (u32 t = 0; t < count; ++t) {
        const u32 j = t + Random(c) % (count - t);
        std::swap(order[t], order[j]);
    }
    generated::Write32(p + 0x40, Random(c) % count);
}

u32 ChoiceBits(u32 p, u32 song) {
    return generated::ReadU32(generated::ReadU32(p + kTable) + song * kEntry + kChoice);
}

/* 0x003FB8B0(player, song): the order's place (random) or the song before
   it (in order), for a song chosen in the menu. */
void SetCurrent(lifted::Context& c) {
    const u32 p = u32(c.r[4]);
    const u32 count = generated::ReadU32(p + kCount);
    if (!St().installed || count <= kInOrderMax) return F_003FB8B0(c);
    if (generated::ReadU32(p + 0x44) != count) Reshuffle(c, p, count);
    if (generated::ReadU32(p + 0xCC) == 1) {
        s32 before = s32(u32(c.r[5])) - 1;
        if (before < 0) before = s32(count) - 1;
        generated::Write32(p + kCurrent, u32(before));
    } else {
        for (s32 i = 0; i < s32(generated::ReadU32(p + 0x44)); ++i)
            if (u64(St().order[i]) == c.r[5]) generated::Write32(p + 0x40, u32(i));
    }
}

/* 0x003FBDB0(player): the next song to play (-1: none, with +0x0C set). */
void NextSong(lifted::Context& c) {
    const u32 p = u32(c.r[4]);
    if (!St().installed || generated::ReadU32(p + kCount) <= kInOrderMax) return F_003FBDB0(c);
    // Which of the choice's bits counts here: 4 in the menus, in a race 2 or
    // 1 by 0x00131E40 (the profile's).
    u32 mask = 4;
    if (generated::ReadU64(0x0064D370u) != 0) {
        u64 saved[32];
        std::memcpy(saved, c.r, sizeof saved);
        c.r[4] = 0x004EE040u;
        lifted::Call(c, 0x00131E40u, u32(saved[31]));
        const u32 v = u32(c.r[2]);
        std::memcpy(c.r, saved, sizeof saved);
        mask = v ? 2 : 1;
    }
    generated::Write8(p + 0x0C, 0);
    const u32 mode = generated::ReadU32(p + 0xCC);
    s32 song = -1;
    const bool on = generated::ReadU8(p + 0xBC) != 0;
    if (mode == 2) {                                    // random
        u32 tried = 0;
        for (; tried < generated::ReadU32(p + kCount); ++tried) {
            const u32 count = generated::ReadU32(p + kCount);
            if (generated::ReadU32(p + 0x44) != count) Reshuffle(c, p, count);
            const u32 at = generated::ReadU32(p + 0x40), n = generated::ReadU32(p + 0x44);
            song = St().order[at];
            generated::Write32(p + 0x40, u32(s32(at + 1) % s32(n)));
            if (!on || (ChoiceBits(p, u32(song)) & mask)) break;
        }
        if (tried == generated::ReadU32(p + kCount)) {
            generated::Write8(p + 0x0C, 1);
            song = -1;
        }
    } else if (mode == 3 || mode == 4) {                // the same again
        song = s32(generated::ReadU32(p + kCurrent));
    } else if (mode == 1 || mode == 5) {                // in order
        const u32 count = generated::ReadU32(p + kCount);
        u32 tried = 0;
        for (; tried < count; ++tried) {
            song = s32((generated::ReadU32(p + kCurrent) + tried + 1) % count);
            if (!on || (ChoiceBits(p, u32(song)) & mask)) break;
        }
        if (tried == count) {
            song = -1;
            generated::Write8(p + 0x0C, 1);
        }
    }
    c.r[2] = u64(s64(song));
}

// ------------------------------------------------------------ overrides: playing

bool IsFake(lifted::Context& c) { return St().fake_bound && u32(c.r[4]) == kFakeStream; }

void SetStreamState(u32 state) { generated::Write32(kFakeStream + kStreamState, state); }

/* 0x00386B30(manager, ...): a stream made - for the player's custom song, the
   stand-in. */
void StreamCreate(lifted::Context& c) {
    State& st = St();
    const u32 ra = u32(c.r[31]), p = u32(c.r[16]);
    const bool from_player = std::find(std::begin(kCreateFromPlayer), std::end(kCreateFromPlayer), ra) !=
                             std::end(kCreateFromPlayer);
    if (from_player && Custom(generated::ReadU32(p + kCurrent)) && !st.fake_bound) {
        for (u32 a = kFakeStream; a < kFakeStream + 0x400; a += 4) generated::Write32(a, 0);
        st.fake_bound = true;
        st.fake_song = generated::ReadU32(p + kCurrent);
        c.r[2] = kFakeStream;
        return;
    }
    F_00386B30(c);
}

/* 0x003865A0(stream, path, 0, from, segment, ...): opened - true once ready.
   The MP3 loaded, held, from `from` milliseconds in (the player's place to
   go on from: its +0xD4). */
void StreamOpen(lifted::Context& c) {
    if (!IsFake(c)) return F_003865A0(c);
    State& st = St();
    const u32 from_ms = u32(c.r[7]);
    const Song& song = st.songs[st.fake_song - kGameSongs];
    if (ps2host::MusicPlay(song.file.u8string(), from_ms / 1000.0, true)) {
        std::fprintf(stderr, "[custom songs] song %u: %s (from %.1f s)\n", unsigned(st.fake_song + 1),
                     song.file.filename().u8string().c_str(), from_ms / 1000.0);
        SetStreamState(kStreamOpen);
    } else {
        SetStreamState(kStreamEnded);           // unreadable: over at once (the next song)
    }
    c.r[2] = 1;
}

/* 0x00386290(stream, ...): played. */
void StreamPlay(lifted::Context& c) {
    if (!IsFake(c)) return F_00386290(c);
    if (generated::ReadU32(kFakeStream + kStreamState) != kStreamEnded) {
        SetStreamState(kStreamPlaying);
        ps2host::MusicPause(false);
    }
}

/* 0x00386300(stream, a1 channel or -1, f12 volume). */
void StreamVolume(lifted::Context& c) {
    if (!IsFake(c)) return F_00386300(c);
    float volume;
    std::memcpy(&volume, &c.f[12], 4);
    ps2host::MusicSetVolume(std::clamp(volume, 0.0f, 1.0f) * kLevel);
}

/* 0x00386240(stream): stopped. */
void StreamStop(lifted::Context& c) {
    if (!IsFake(c)) return F_00386240(c);
    const u32 state = generated::ReadU32(kFakeStream + kStreamState);
    if (state != kStreamStopped && state != kStreamEnded) SetStreamState(kStreamStopped);
    ps2host::MusicPause(true);
}

/* 0x003863C0(stream): closed. */
void StreamClose(lifted::Context& c) {
    if (!IsFake(c)) return F_003863C0(c);
    ps2host::MusicStop();
    SetStreamState(kStreamIdle);
}

/* 0x00386A60(manager, stream): given back. */
void StreamRelease(lifted::Context& c) {
    State& st = St();
    if (st.fake_bound && u32(c.r[5]) == kFakeStream) {
        ps2host::MusicStop();
        st.fake_bound = false;
        return;
    }
    F_00386A60(c);
}

/* 0x00386080(stream, a1): where it is, in milliseconds. */
void StreamPosition(lifted::Context& c) {
    if (!IsFake(c)) return F_00386080(c);
    c.r[2] = u64(s64(s32(ps2host::MusicPosition() * 1000.0)));
}

/* 0x00386020(stream, ...): the step its position goes back in (the player
   rounds a place to go on from up to it). */
void StreamStep(lifted::Context& c) {
    if (!IsFake(c)) return F_00386020(c);
    c.r[2] = 1;
}

/* 0x003FC8C0: the player's frame. A custom song played to its end clears the
   card's byte for it (0x004F5040 + the song, past the game's 44: the other
   audio settings) - kept as it was. */
void PlayerFrame(lifted::Context& c) {
    State& st = St();
    st.player = u32(c.r[4]);
    const u32 song = generated::ReadU32(st.player + kCurrent);
    if (!Custom(song)) return F_003FC8C0(c);
    const u32 byte = kProfileSongs + song;
    const u8 kept = u8(generated::ReadU8(byte));
    F_003FC8C0(c);
    generated::Write8(byte, kept);
}

}  // namespace

/* At the display's frame step (mods.cpp): the stand-in stream's state as the
   MP3 goes; the folder looked at again now and then. */
void CustomSongsFrame() {
    State& st = St();
    if (st.fake_bound && generated::ReadU32(kFakeStream + kStreamState) == kStreamPlaying && ps2host::MusicEnded()) {
        std::fprintf(stderr, "[custom songs] song %u ended\n", unsigned(st.fake_song + 1));
        SetStreamState(kStreamEnded);
    }
    if (!st.scanned || !st.area_ok) return;
    // The player's list ours again after the menu puts back its copy.
    if (st.installed && st.player && generated::ReadU32(st.player + kTable) == kTable0)
        generated::Write32(st.player + kCount, kGameSongs + u32(st.songs.size()));
    // Songs added or taken away: the list again, while no custom song plays.
    static int frames = 0;
    if (++frames < 180) return;
    frames = 0;
    if (st.fake_bound) return;
    const std::vector<Song> songs = Scan();
    bool same = songs.size() == st.songs.size();
    for (std::size_t i = 0; same && i < songs.size(); ++i) same = songs[i].file == st.songs[i].file;
    if (same) return;
    if (st.installed) KeepChoices();
    if (!songs.empty() || st.installed) Build(songs);
}

void RegisterCustomSongs() {
    ps2host::Override(0x003FCD20u, PlayerInit, "EA Trax's player made: custom songs listed");
    ps2host::Override(0x003FCC10u, CardChoices, "EA Trax: the card's choices, custom songs' from the file");
    ps2host::Override(0x003FBB50u, DescriptorCopy, "EA Trax: the card's 44 bytes, custom songs' in the file");
    ps2host::Override(0x003FB8B0u, SetCurrent, "EA Trax: the random order past 51 songs");
    ps2host::Override(0x003FBDB0u, NextSong, "EA Trax: the next song past 51 songs");
    ps2host::Override(0x003FC8C0u, PlayerFrame, "EA Trax: a custom song's end keeps the card");
    ps2host::Override(0x00271A60u, StringLookup, "strings: custom songs' names");
    ps2host::Override(0x00386B30u, StreamCreate, "RWA stream made: a custom song's stand-in");
    ps2host::Override(0x003865A0u, StreamOpen, "RWA stream opened: a custom song's MP3");
    ps2host::Override(0x00386290u, StreamPlay, "RWA stream played: a custom song's MP3");
    ps2host::Override(0x00386300u, StreamVolume, "RWA stream volume: a custom song's MP3");
    ps2host::Override(0x00386240u, StreamStop, "RWA stream stopped: a custom song's MP3");
    ps2host::Override(0x003863C0u, StreamClose, "RWA stream closed: a custom song's MP3");
    ps2host::Override(0x00386A60u, StreamRelease, "RWA stream given back: a custom song's stand-in");
    ps2host::Override(0x00386080u, StreamPosition, "RWA stream position: a custom song's MP3");
    ps2host::Override(0x00386020u, StreamStep, "RWA stream step: a custom song's MP3");
}
